/*
 * Copyright (C) 2018 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "menu.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QVariantList>
#include <QTimer>
#include <algorithm>

#include "utils.h"

static const QString s_orgGtkMenus = QStringLiteral("org.gtk.Menus");
static const int START_INDEX = 100;

Menu::Menu(const QString &serviceName, const QString &objectPath, bool menubar, QObject *parent) : QObject(parent)
    , menubar(menubar)
    , m_serviceName(serviceName)
    , m_objectPath(objectPath)
{
    Q_ASSERT(!serviceName.isEmpty());
    Q_ASSERT(!m_objectPath.isEmpty());

    if (!QDBusConnection::sessionBus().connect(m_serviceName,
                                            m_objectPath,
                                            s_orgGtkMenus,
                                            QStringLiteral("Changed"),
                                            this,
                                            SLOT(onMenuChanged(GMenuChangeList)))) {
        qDebug() << "Failed to subscribe to menu changes for" << parent << "on" << serviceName << "at" << objectPath;
    }
}

Menu::~Menu() = default;

void Menu::cleanup()
{
    stop(m_subscriptions);
}

void Menu::start(uint id)
{
    if (m_subscriptions.contains(id) || m_testings.contains(id))
        return;

    m_testings.insert(id);

    if(!menubar && id == 0) {
        QTimer::singleShot(20, [this, id]{
            if (!m_menus.contains(id)) {
                m_testings.remove(id);

                m_menus[id].append(GMenuItem(0, 0, VariantMapList{QVariantMap{{":section", QVariant::fromValue(GMenuSection(0, 1))}}}));
                m_menus[id].append(GMenuItem(0, 1, VariantMapList{QVariantMap{{":submenu", QVariant::fromValue(GMenuSection(START_INDEX, 0))}, {"label", "菜单"}}}));

                m_subscriptions.insert(id);
                emit menuAppeared();
            }

            emit subscribed(id);
        });
        return;
    }

    // TODO watch service disappearing?

    // dbus-send --print-reply --session --dest=:1.103 /org/libreoffice/window/104857641/menus/menubar org.gtk.Menus.Start array:uint32:0

    QDBusMessage msg = QDBusMessage::createMethodCall(m_serviceName,
                                                    m_objectPath,
                                                    s_orgGtkMenus,
                                                    QStringLiteral("Start"));
    msg.setArguments({
        QVariant::fromValue(QList<uint>{!menubar && id == START_INDEX ? 0 :id})
    });

    QDBusPendingReply<GMenuItemList> reply = QDBusConnection::sessionBus().asyncCall(msg);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, id](QDBusPendingCallWatcher *watcher) {
        QScopedPointer<QDBusPendingCallWatcher, QScopedPointerDeleteLater> watcherPtr(watcher);

        QDBusPendingReply<GMenuItemList> reply = *watcherPtr;
        m_testings.remove(id);

        if (reply.isError()) {
            qDebug() << "Failed to start subscription to" << id << "on" << m_serviceName << "at" << m_objectPath << reply.error();

            emit failedToSubscribe(id);
        } else {
            const bool hadMenu = !m_menus.isEmpty();

            auto menus = reply.value();

            // LibreOffice on startup fails to give us some menus right away, we'll also subscribe in onMenuChanged() if necessary
            if (menus.isEmpty()) {
                qDebug() << "Got an empty menu for" << id << "on" << m_serviceName << "at" << m_objectPath;
                return;
            }

            for(auto it=menus.begin(); it!=menus.end(); it++) {
                if(!menubar && id==START_INDEX && it->id==0)
                    it->id = START_INDEX;

                for(auto iter = it->items.begin(); iter != it->items.end(); iter++) {
                    if(iter->contains(":section")) {
                        GMenuSection section = qdbus_cast<GMenuSection>(iter->value(":section").value<QDBusArgument>());
                        if(!menubar && id==START_INDEX && section.subscription==0) section.subscription = START_INDEX;
                        iter->insert(":section", QVariant::fromValue(section));
                    } else if(iter->contains(":submenu")) {
                        GMenuSection section = qdbus_cast<GMenuSection>(iter->value(":submenu").value<QDBusArgument>());
                        if(!menubar && id==START_INDEX && section.subscription==0) section.subscription = START_INDEX;
                        iter->insert(":submenu", QVariant::fromValue(section));
                    }
                }
            }

            m_menus[id].append(menus);
            // TODO are we subscribed to all it returns or just to the ones we requested?
            m_subscriptions.insert(id);

            // do we have a menu now? let's tell everyone
            if (!hadMenu && !m_menus.isEmpty()) {
                emit menuAppeared();
            }

            emit subscribed(id);
        }
    });
}

void Menu::stop(const QSet<uint> &ids)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(m_serviceName,
                                                    m_objectPath,
                                                    s_orgGtkMenus,
                                                    QStringLiteral("End"));
    msg.setArguments({
        QVariant::fromValue(ids) // don't let it unwrap it, hence in a variant
    });

    QDBusPendingReply<void> reply = QDBusConnection::sessionBus().asyncCall(msg);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, ids](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<void> reply = *watcher;
        if (reply.isError()) {
            qDebug() << "Failed to stop subscription to" << ids << "on" << m_serviceName << "at" << m_objectPath << reply.error();
        } else {
            // remove all subscriptions that we unsubscribed from
            // TODO is there a nicer algorithm for that?
            // TODO remove all m_menus also?
            // m_subscriptions.erase(std::remove_if(m_subscriptions.begin(), m_subscriptions.end(),
            //                           std::bind(&QList<uint>::contains, m_subscriptions, std::placeholders::_1)),
            //                       m_subscriptions.end());

            for(auto id : ids)
            {
                m_subscriptions.remove(id);
                m_menus.remove(id);
            }

            if (m_subscriptions.isEmpty()) {
                emit menuDisappeared();
            }
        }
        watcher->deleteLater();
    });
}

bool Menu::hasMenu() const
{
    return !m_menus.isEmpty();
}

bool Menu::hasSubscription(uint subscription) const
{
    return m_subscriptions.contains(subscription);
}

GMenuItem Menu::getSection(int id, bool *ok) const
{
    int subscription;
    int section;
    int index;
    Utils::intToTreeStructure(id, subscription, section, index);
    return getSection(subscription, section, ok);
}

GMenuItem Menu::getSection(int subscription, int section, bool *ok) const
{
    const auto menu = m_menus.value(subscription);

    auto it = std::find_if(menu.begin(), menu.end(), [section](const GMenuItem &item) {
        return item.section == section;
    });

    if (it == menu.end()) {
        if (ok) {
            *ok = false;
        }
        return GMenuItem();
    }

    if (ok) {
        *ok = true;
    }
    return *it;
}

QVariantMap Menu::getItem(int id) const
{
    int subscription;
    int section;
    int index;
    Utils::intToTreeStructure(id, subscription, section, index);
    return getItem(subscription, section, index);
}

QVariantMap Menu::getItem(int subscription, int sectionId, int index) const
{
    bool ok;
    const GMenuItem section = getSection(subscription, sectionId, &ok);

    if (!ok) {
        return QVariantMap();
    }

    const auto items = section.items;

    if (items.count() < index) {
        qDebug() << "Cannot get action" << subscription << sectionId << index << "which is out of bounds";
        return QVariantMap();
    }

    return items.at(index);
}

void Menu::onMenuChanged(const GMenuChangeList &changes)
{
    const bool hadMenu = !m_menus.isEmpty();

    QSet<uint> dirtyMenus;
    QSet<uint> dirtyItems;

    auto recurseRemove = [this](QVariantMap source) {
        QSet<uint> ids;
        std::function<void(QVariantMap)> reFind = [&ids, this, &reFind](QVariantMap source){
            if(source.contains(QLatin1String(":submenu")))
            {
                GMenuSection gmenuSection = source.value(QLatin1String(":submenu")).value<GMenuSection>();
                if(m_menus.contains(gmenuSection.subscription))
                {
                    for(auto item : m_menus.value(gmenuSection.subscription))
                    {
                        for(auto subSource : item.items)
                            reFind(subSource);
                    }
                    ids.insert(gmenuSection.subscription);
                }
            }
        };

        reFind(source);

        if(!ids.isEmpty())
            stop(ids);
    };

    auto updateSection = [&dirtyItems, &dirtyMenus, this](const int subscription, const GMenuChange &change, GMenuItem &section) {
        const bool reIndex = !menubar && subscription == START_INDEX;
        const bool updateItem = change.itemsToRemoveCount == change.itemsToInsert.count();
        // Check if the amount of inserted items is identical to the items to be removed,
        // just update the existing items and signal a change for that.
        // LibreOffice tends to do that e.g. to update its Undo menu entry

        for (int i = 0; i < change.itemsToRemoveCount; ++i) {
            if(section.items.count() > change.changePosition)
            {
                QVariantMap source = section.items.takeAt(change.changePosition);
                // recurseRemove(source);
            } else
                break;
        }

        for (int i = 0; i < change.itemsToInsert.count(); ++i) {
            QVariantMap map = change.itemsToInsert.at(i);
            if(map.contains(":section")) {
                GMenuSection sec = qdbus_cast<GMenuSection>(map.value(":section").value<QDBusArgument>());
                if(reIndex && sec.subscription==0) sec.subscription = START_INDEX;
                map[":section"].setValue(QVariant::fromValue(sec));
            } else if(map.contains(":submenu")) {
                GMenuSection sec = qdbus_cast<GMenuSection>(map.value(":submenu").value<QDBusArgument>());
                if(reIndex && sec.subscription==0) sec.subscription = START_INDEX;
                map[":submenu"].setValue(QVariant::fromValue(sec));
            }

            if(section.items.count() > change.changePosition + i)
                section.items.insert(change.changePosition + i, map);
            else
                section.items << map;

            // 0 is the menu itself, items start at 1
            if(updateItem)
                dirtyItems.insert(Utils::treeStructureToInt(subscription, change.section, change.changePosition + i));
        }

        if(!updateItem)
            dirtyMenus.insert(Utils::treeStructureToInt(subscription, change.section, 0));
    };

    for (const auto &change : changes) {
        const int subscription = !menubar && change.subscription == 0 ? START_INDEX : change.subscription;
        // shouldn't happen, it says only Start() subscribes to changes
        if (!m_subscriptions.contains(change.subscription)) {
            qDebug() << "Got menu change for menu" << change.subscription << "that we are not subscribed to, subscribing now";
            // LibreOffice doesn't give us a menu right away but takes a while and then signals us a change
            start(change.subscription);
            continue;
        }

        auto &menu = m_menus[subscription];

        bool sectionFound = false;
        // TODO findSectionRef
        for (GMenuItem &section : menu) {
            if (section.section == change.section) {
                qDebug() << "Updating existing section" << change.section << "in subscription" << change.subscription;

                sectionFound = true;
                updateSection(subscription, change, section);
                break;
            }
        }

        // Insert new section
        if (!sectionFound) {
            qDebug() << "Creating new section" << change.section << "in subscription" << change.subscription;

            if (change.itemsToRemoveCount > 0) {
                qDebug() << "Menu change requested to remove items from a new (and as such empty) section";
            }

            GMenuItem newSection;
            newSection.id = subscription;
            newSection.section = change.section;
            updateSection(subscription, change, newSection);
            menu.append(newSection);
        }
    }

    // do we have a menu now? let's tell everyone
    if (!hadMenu && !m_menus.isEmpty()) {
        emit menuAppeared();
    } else if (hadMenu && m_menus.isEmpty()) {
        emit menuDisappeared();
    }

    if (!dirtyItems.isEmpty())
        emit itemsChanged(dirtyItems);

    if(!dirtyMenus.isEmpty())
        emit menusChanged(dirtyMenus);
}

void Menu::actionsChanged(const QStringList &dirtyActions, const QString &prefix)
{
    QSet<uint> dirtyItems;
    QSet<QString> prefixActions;
    for (const QString &action : dirtyActions) prefixActions.insert(prefix + action);

    for (auto it = m_menus.constBegin(), end = m_menus.constEnd(); it != end; ++it) {
        const int subscription = it.key();

        for (const auto &menu : it.value()) {
            const int section = menu.section;

            int count = 0;
            // const auto items = menu.items;
            for (const auto &item : menu.items) {
                if (prefixActions.contains(Utils::itemActionName(item)))
                    dirtyItems.insert(Utils::treeStructureToInt(subscription, section, count));
                ++count;
            }
        }
    }

    if (!dirtyItems.isEmpty())
        emit itemsChanged(dirtyItems);
}

