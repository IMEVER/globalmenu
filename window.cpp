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

#include "window.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QList>
#include <QMutableListIterator>
#include <QVariantList>
#include <algorithm>

#include "actions.h"
#include "icons.h"
#include "menu.h"
#include "utils.h"

#include "dbusmenushortcut_p.h"
#include "dbusmenuadaptor.h"

static const QString s_applicationActionsPrefix = QStringLiteral("app.");
static const QString s_unityActionsPrefix = QStringLiteral("unity.");
static const QString s_windowActionsPrefix = QStringLiteral("win.");

Window::Window(const QString &serviceName) : QObject()
    , m_serviceName(serviceName)
{
    qDebug() << "Created menu on" << serviceName;

    Q_ASSERT(!serviceName.isEmpty());
}

Window::~Window() {}

void Window::init()
{
    qDebug() << "Inited window with menu for" << m_winId << "on" << m_serviceName << "at app" << m_applicationObjectPath << "win" << m_windowObjectPath << "unity" << m_unityObjectPath;

    if (!m_applicationMenuObjectPath.isEmpty()) {
        m_applicationMenu = new Menu(m_serviceName, m_applicationMenuObjectPath, false, this);
        connect(m_applicationMenu, &Menu::menuAppeared, this, &Window::updateWindowProperties);
        connect(m_applicationMenu, &Menu::menuDisappeared, this, &Window::updateWindowProperties);
        connect(m_applicationMenu, &Menu::subscribed, this, &Window::onMenuSubscribed);
        // basically so it replies on DBus no matter what
        connect(m_applicationMenu, &Menu::failedToSubscribe, this, &Window::onMenuSubscribed);
        connect(m_applicationMenu, &Menu::itemsChanged, this, &Window::menuItemsChanged);
        connect(m_applicationMenu, &Menu::menusChanged, this, &Window::menuChanged);
    }

    if (!m_menuBarObjectPath.isEmpty()) {
        m_menuBar = new Menu(m_serviceName, m_menuBarObjectPath, true, this);
        connect(m_menuBar, &Menu::menuAppeared, this, &Window::updateWindowProperties);
        connect(m_menuBar, &Menu::menuDisappeared, this, &Window::updateWindowProperties);
        connect(m_menuBar, &Menu::subscribed, this, &Window::onMenuSubscribed);
        connect(m_menuBar, &Menu::failedToSubscribe, this, &Window::onMenuSubscribed);
        connect(m_menuBar, &Menu::itemsChanged, this, &Window::menuItemsChanged);
        connect(m_menuBar, &Menu::menusChanged, this, &Window::menuChanged);
    }

    if (!m_applicationObjectPath.isEmpty()) {
        m_applicationActions = new Actions(m_serviceName, m_applicationObjectPath, this);
        connect(m_applicationActions, &Actions::actionsChanged, this, [this](const QStringList &dirtyActions) {
            onActionsChanged(dirtyActions, s_applicationActionsPrefix);
        });
        connect(m_applicationActions, &Actions::loaded, this, [this] {
            if (m_menuInited) {
                onActionsChanged(m_applicationActions->getAll().keys(), s_applicationActionsPrefix);
            } else {
                initMenu();
            }
        });
        m_applicationActions->load();
    }

    if (!m_unityObjectPath.isEmpty()) {
        m_unityActions = new Actions(m_serviceName, m_unityObjectPath, this);
        connect(m_unityActions, &Actions::actionsChanged, this, [this](const QStringList &dirtyActions) {
            onActionsChanged(dirtyActions, s_unityActionsPrefix);
        });
        connect(m_unityActions, &Actions::loaded, this, [this] {
            if (m_menuInited) {
                onActionsChanged(m_unityActions->getAll().keys(), s_unityActionsPrefix);
            } else {
                initMenu();
            }
        });
        m_unityActions->load();
    }

    if (!m_windowObjectPath.isEmpty()) {
        m_windowActions = new Actions(m_serviceName, m_windowObjectPath, this);
        connect(m_windowActions, &Actions::actionsChanged, this, [this](const QStringList &dirtyActions) {
            onActionsChanged(dirtyActions, s_windowActionsPrefix);
        });
        connect(m_windowActions, &Actions::loaded, this, [this] {
            if (m_menuInited) {
                onActionsChanged(m_windowActions->getAll().keys(), s_windowActionsPrefix);
            } else {
                initMenu();
            }
        });
        m_windowActions->load();
    }
}

WId Window::winId() const
{
    return m_winId;
}

void Window::setWinId(WId winId)
{
    m_winId = winId;
}

QString Window::serviceName() const
{
    return m_serviceName;
}

QString Window::applicationObjectPath() const
{
    return m_applicationObjectPath;
}

void Window::setApplicationObjectPath(const QString &applicationObjectPath)
{
    m_applicationObjectPath = applicationObjectPath;
}

QString Window::unityObjectPath() const
{
    return m_unityObjectPath;
}

void Window::setUnityObjectPath(const QString &unityObjectPath)
{
    m_unityObjectPath = unityObjectPath;
}

QString Window::applicationMenuObjectPath() const
{
    return m_applicationMenuObjectPath;
}

void Window::setApplicationMenuObjectPath(const QString &applicationMenuObjectPath)
{
    m_applicationMenuObjectPath = applicationMenuObjectPath;
}

QString Window::menuBarObjectPath() const
{
    return m_menuBarObjectPath;
}

void Window::setMenuBarObjectPath(const QString &menuBarObjectPath)
{
    m_menuBarObjectPath = menuBarObjectPath;
}

QString Window::windowObjectPath() const
{
    return m_windowObjectPath;
}

void Window::setWindowObjectPath(const QString &windowObjectPath)
{
    m_windowObjectPath = windowObjectPath;
}

QString Window::currentMenuObjectPath() const
{
    return m_currentMenuObjectPath;
}

QString Window::proxyObjectPath() const
{
    return m_proxyObjectPath;
}

void Window::initMenu()
{
    if (m_menuInited) return;

    if (!registerDBusObject()) return;

    // appmenu-gtk-module always announces a menu bar on every GTK window even if there is none
    // so we subscribe to the menu bar as soon as it shows up so we can figure out
    // if we have a menu bar, an app menu, or just nothing
    if (m_applicationMenu) m_applicationMenu->start(0);

    if (m_menuBar) m_menuBar->start(0);

    m_menuInited = true;
}

void Window::menuItemsChanged(const QSet<uint> &itemIds)
{
    if (qobject_cast<Menu*>(sender()) == m_currentMenu) {
        DBusMenuItemList items;

        for (uint id : itemIds) {
            const auto newItem = m_currentMenu->getItem(id);

            DBusMenuItem dBusItem{
                // 0 is menu, items start at 1
                static_cast<int>(id),
                gMenuToDBusMenuProperties(newItem)
            };
            items.append(dBusItem);
        }

        emit ItemsPropertiesUpdated(items, {});
    }
}

void Window::menuChanged(const QSet<uint> &menuIds)
{
    if (qobject_cast<Menu*>(sender()) == m_currentMenu) {
        QSet<int> sids;
        for (uint id : menuIds) {
            int subscription, section, index;
            Utils::intToTreeStructure(id, subscription, section, index);
            sids.insert(subscription);
        }
        for(auto subscription : sids)
            emit LayoutUpdated(3 /*revision*/, subscription);
    }
}

void Window::onMenuSubscribed(uint id)
{
    // When it was a delayed GetLayout request, send the reply now
    const auto pendingReplies = m_pendingGetLayouts.values(id);
    if (!pendingReplies.isEmpty()) {
        for (const auto &pendingReply : pendingReplies) {
            if (pendingReply.type() != QDBusMessage::InvalidMessage) {
                auto reply = pendingReply.createReply();

                DBusMenuLayoutItem item;
                uint revision = GetLayout(Utils::treeStructureToInt(id, 0, 0), 1, {}, item);

                reply << revision << QVariant::fromValue(item);

                QDBusConnection::sessionBus().send(reply);
            }
        }
        m_pendingGetLayouts.remove(id);
    } else {
        emit LayoutUpdated(2 /*revision*/, id);
    }
}

bool Window::getAction(const QString &name, GMenuAction &action) const
{
    QString lookupName;
    if(Actions *actions = getActionsForAction(name, lookupName))
        return actions->get(lookupName, action);
    return false;
}

void Window::triggerAction(const QString &name, const QVariant &target, uint timestamp)
{
    QString lookupName;
    if(Actions *actions = getActionsForAction(name, lookupName))
        actions->trigger(lookupName, target, timestamp);
}

Actions *Window::getActionsForAction(const QString &name, QString &lookupName) const
{
    if (name.startsWith(QLatin1String("app."))) {
        lookupName = name.mid(4);
        return m_applicationActions;
    } else if (name.startsWith(QLatin1String("unity."))) {
        lookupName = name.mid(6);
        return m_unityActions;
    } else if (name.startsWith(QLatin1String("win."))) {
        lookupName = name.mid(4);
        return m_windowActions;
    }

    return nullptr;
}

void Window::onActionsChanged(const QStringList &dirty, const QString &prefix)
{
    if (m_applicationMenu) {
        m_applicationMenu->actionsChanged(dirty, prefix);
    }
    if (m_menuBar) {
        m_menuBar->actionsChanged(dirty, prefix);
    }
}

bool Window::registerDBusObject()
{
    Q_ASSERT(m_proxyObjectPath.isEmpty());

    static int menus = 0;
    ++menus;

    new DbusmenuAdaptor(this);

    const QString objectPath = QStringLiteral("/me/imever/dde/TopPanel/MenuBar/%1").arg(QString::number(menus));
    qDebug() << "Registering DBus object path" << objectPath;

    if (!QDBusConnection::sessionBus().registerObject(objectPath, this)) {
        qDebug() << "Failed to register object " << objectPath;
        return false;
    }

    m_proxyObjectPath = objectPath;

    return true;
}

void Window::updateWindowProperties()
{
    const bool hasMenu = ((m_applicationMenu && m_applicationMenu->hasMenu()) || (m_menuBar && m_menuBar->hasMenu()));

    if (!hasMenu) {
        emit requestRemoveWindowProperties();
        return;
    }

    Menu *oldMenu = m_currentMenu;
    Menu *newMenu = qobject_cast<Menu*>(sender());
    // set current menu as needed
    if (!m_currentMenu) {
        m_currentMenu = newMenu;
    // Menu Bar takes precedence over application menu
    } else if (m_currentMenu == m_applicationMenu && newMenu == m_menuBar) {
        qDebug() << "Switching from application menu to menu bar";
        m_currentMenu = newMenu;
        // TODO update layout
    }

    if (m_currentMenu != oldMenu) {
        // update entire menu now
        emit LayoutUpdated(4 /*revision*/, 0);
    }

    emit requestWriteWindowProperties();
}

// DBus
bool Window::AboutToShow(int id)
{
    // We always request the first time GetLayout is called and keep up-to-date internally
    // No need to have us prepare anything here
    Q_UNUSED(id);
    return false;
}

void Window::Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp)
{
    Q_UNUSED(data);

    if (!m_currentMenu) return;

    // GMenu dbus doesn't have any "opened" or "closed" signals, we'll only handle "clicked"

    if (eventId == QLatin1String("clicked")) {
        const QVariantMap item = m_currentMenu->getItem(id);

        if(item.contains(QLatin1String(":submenu"))) return;

        const QString action = item.value(QStringLiteral("action")).toString();
        const QVariant target = item.value(QStringLiteral("target"));
        if (!action.isEmpty())
            triggerAction(action, target, timestamp);
    }
}

DBusMenuItemList Window::GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames)
{
    DBusMenuItemList retValues;

    if(m_currentMenu && !ids.isEmpty()) {
        for(const auto id : ids) {
            DBusMenuItem item;
            item.id = id;

            int subscription, sectionId, indexId;
            Utils::intToTreeStructure(id, subscription, sectionId, indexId);

            if (m_currentMenu->hasSubscription(subscription)) {
                bool ok;
                GMenuItem section = m_currentMenu->getSection(subscription, sectionId, &ok);

                if (ok && (section.items.count() > indexId)) {
                    auto tmpItem = section.items.at(indexId);
                    if(propertyNames.isEmpty())
                        item.properties.insert(tmpItem);
                    else
                        for(auto &property : propertyNames)
                            if(tmpItem.contains(property))
                                item.properties.insert(property, tmpItem.value(property));
                }
            }

            retValues.append(item);
        }
    }

    return retValues;
}

uint Window::GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames, DBusMenuLayoutItem &dbusItem)
{
    if (!m_currentMenu) {
        return 1;
    }

    int subscription, sectionId, indexId;
    Utils::intToTreeStructure(parentId, subscription, sectionId, indexId);

    if (!m_currentMenu->hasSubscription(subscription)) {
        // let's serve multiple similar requests in one go once we've processed them
        m_pendingGetLayouts.insert(subscription, message());
        setDelayedReply(true);

        m_currentMenu->start(subscription);
        return 1;
    }

    bool ok;
    GMenuItem section = m_currentMenu->getSection(subscription, sectionId, &ok);

    if (!ok || (section.items.count() < indexId)) {
        qDebug() << "There is no section on" << subscription << "at" << 0 << "with" << indexId;
        return 1;
    }

    auto tmpItem = section.items.at(indexId);
    if(tmpItem.contains(QLatin1String(":submenu")))
    {
        GMenuSection gmenuSection = tmpItem.value(QLatin1String(":submenu")).value<GMenuSection>();
        subscription = gmenuSection.subscription;
        sectionId = gmenuSection.section;
        indexId = 0;

        if (!m_currentMenu->hasSubscription(subscription)) {
            // let's serve multiple similar requests in one go once we've processed them
            m_pendingGetLayouts.insert(subscription, message());
            setDelayedReply(true);

            m_currentMenu->start(subscription);
            return 1;
        }

        section = m_currentMenu->getSection(subscription, sectionId, &ok);

        if (!ok || (section.items.count() < indexId)) {
            qDebug() << "There is no section on" << subscription << "at" << 0 << "with" << indexId;
            return 1;
        }
    }

    dbusItem.id = Utils::treeStructureToInt(subscription, sectionId, indexId); // TODO
    dbusItem.properties = {
        {QStringLiteral("children-display"), QStringLiteral("submenu")}
    };

    const auto itemsToBeAdded = section.items;
    const int count = itemsToBeAdded.count();
    int index = 0;
    for (const auto &item : itemsToBeAdded) {
        // Now resolve section aliases
        auto it = item.constFind(QStringLiteral(":section"));
        if (it != item.constEnd()) {//qDebug()<<subscription<<"\t"<<sectionId<<"\t"<<indexId<<"\t";
            // references another place, add it instead
            GMenuSection gmenuSection = it->value<GMenuSection>();
            // remember where the item came from and give it an appropriate ID
            // so updates signalled by the app will map to the right place
            int originalSubscription = gmenuSection.subscription;
            int originalMenu = gmenuSection.section;

            // TODO start subscription if we don't have it
            auto items = m_currentMenu->getSection(gmenuSection.subscription, gmenuSection.section).items;

            // Check whether it's an alias to an alias
            // FIXME make generic/recursive
            while (items.count() == 1) {
                const auto &aliasedItem = items.constFirst();
                auto findIt = aliasedItem.constFind(QStringLiteral(":section"));
                if (findIt != aliasedItem.constEnd()) {
                    GMenuSection gmenuSection2 = findIt->value<GMenuSection>();
                    items = m_currentMenu->getSection(gmenuSection2.subscription, gmenuSection2.section).items;

                    originalSubscription = gmenuSection2.subscription;
                    originalMenu = gmenuSection2.section;
                    continue;
                }

                break;
            }

            int aliasedCount = 0;
            for (const auto &aliasedItem : qAsConst(items)) {
                DBusMenuLayoutItem aliasedChild{Utils::treeStructureToInt(originalSubscription, originalMenu, aliasedCount++), gMenuToDBusMenuProperties(aliasedItem), {}};
                dbusItem.children.append(aliasedChild);
            }

            if(count > 1 && index < count - 1)
            {
                QVariantMap result;
                result.insert(QStringLiteral("type"), QStringLiteral("separator"));
                result.insert(QStringLiteral("enabled"), true);
                result.insert(QStringLiteral("visible"), true);

                DBusMenuLayoutItem child{Utils::treeStructureToInt(subscription, sectionId, index), result, {}};
                dbusItem.children.append(child);
            }
        }

        index++;
    }

    // revision, unused in libdbusmenuqt
    return 1;
}

QDBusVariant Window::GetProperty(int id, const QString &property)
{
    QDBusVariant value;

    if (m_currentMenu && !property.isEmpty()) {
        int subscription, sectionId, indexId;
        Utils::intToTreeStructure(id, subscription, sectionId, indexId);

        if (m_currentMenu->hasSubscription(subscription)) {
            bool ok;
            GMenuItem section = m_currentMenu->getSection(subscription, sectionId, &ok);

            if (ok && (section.items.count() > indexId)) {
                auto tmpItem = section.items.at(indexId);
                value.setVariant(tmpItem.value(property, ""));
            }
        }
    }

    return value;
}

QString Window::status() const
{
    return QStringLiteral("normal");
}

uint Window::version() const
{
    return 4;
}

QVariantMap Window::gMenuToDBusMenuProperties(const QVariantMap &source) const
{
    QVariantMap result;

    result.insert(QStringLiteral("label"), source.value(QStringLiteral("label")).toString());

    if (source.contains(QLatin1String(":section"))) {
        result.insert(QStringLiteral("type"), QStringLiteral("separator"));
    }

    const bool isMenu = source.contains(QLatin1String(":submenu"));
    if (isMenu)
        result.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));

    QString accel = source.value(QStringLiteral("accel")).toString();
    if (!accel.isEmpty()) {
        QStringList shortcut;

        // TODO use regexp or something
        if (accel.contains(QLatin1String("<Primary>")) || accel.contains(QLatin1String("<Control>"))) {
            shortcut.append(QStringLiteral("Control"));
            accel.remove(QLatin1String("<Primary>"));
            accel.remove(QLatin1String("<Control>"));
        }

        if (accel.contains(QLatin1String("<Shift>"))) {
            shortcut.append(QStringLiteral("Shift"));
            accel.remove(QLatin1String("<Shift>"));
        }

        if (accel.contains(QLatin1String("<Alt>"))) {
            shortcut.append(QStringLiteral("Alt"));
            accel.remove(QLatin1String("<Alt>"));
        }

        if (accel.contains(QLatin1String("<Super>"))) {
            shortcut.append(QStringLiteral("Super"));
            accel.remove(QLatin1String("<Super>"));
        }

        if (!accel.isEmpty()) {
            // TODO replace "+" by "plus" and "-" by "minus"
            shortcut.append(accel);

            // TODO does gmenu support multiple?
            DBusMenuShortcut dbusShortcut;
            dbusShortcut.append(shortcut); // don't let it unwrap the list we append

            result.insert(QStringLiteral("shortcut"), QVariant::fromValue(dbusShortcut));
        }
    }

    bool enabled = true;

    const QString actionName = Utils::itemActionName(source);

    GMenuAction action;
    // if no action is specified this is fine but if there is an action we don't have
    // disable the menu entry
    bool actionOk = true;
    if (!actionName.isEmpty()) {
        actionOk = getAction(actionName, action);
        enabled = actionOk && action.enabled;
    }

    // we used to only send this if not enabled but then dbusmenuimporter does not
    // update the enabled state when it changes from disabled to enabled
    result.insert(QStringLiteral("enabled"), enabled);

    bool visible = true;
    const QString hiddenWhen = source.value(QStringLiteral("hidden-when")).toString();
    if (hiddenWhen == QLatin1String("action-disabled") && (!actionOk || !enabled))
        visible = false;
    else if (hiddenWhen == QLatin1String("action-missing") && !actionOk)
        visible = false;
    // While we have Global Menu we don't have macOS menu (where Quit, Help, etc is separate)
    else if (hiddenWhen == QLatin1String("macos-menubar"))
        visible = true;

    result.insert(QStringLiteral("visible"), visible);

    QString icon = source.value(QStringLiteral("icon")).toString();
    if (icon.isEmpty())
        icon = source.value(QStringLiteral("verb-icon")).toString();

    if(icon.isEmpty())
        icon = Icons::actionIcon(actionName);

    if (!icon.isEmpty())
        result.insert(QStringLiteral("icon-name"), icon);


    if (actionOk && !isMenu) {
        const auto actionStates = action.state;
        if (actionStates.count() == 1) {
            const auto &actionState = actionStates.first();
            if (actionState.type() == QVariant::Bool) {
                result.insert(QStringLiteral("toggle-type"), QStringLiteral("checkmark"));
                result.insert(QStringLiteral("toggle-state"), actionState.toBool() ? 1 : 0);
            } else if (actionState.type() == QVariant::String) {
                const QVariant target = source.value(QStringLiteral("target"));
                result.insert(QStringLiteral("toggle-type"), QStringLiteral("radio"));
                result.insert(QStringLiteral("toggle-state"), actionState == target ? 1 : 0);
            }
        }
    }

    return result;
}
