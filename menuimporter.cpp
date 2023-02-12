/*
  This file is part of the KDE project.

  Copyright (c) 2011 Lionel Chauvin <megabigbug@yahoo.fr>
  Copyright (c) 2011,2012 Cédric Bellegarde <gnumdk@gmail.com>
  Copyright (c) 2016 Kai Uwe Broulik <kde@privat.broulik.de>

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/

#include "menuimporter.h"
#include "menuimporteradaptor.h"
#include "dbusmenutypes_p.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QX11Info>
#include <QTimer>
#include <KWindowSystem>

static const QString REGISTRAR_SERVICE = "com.canonical.AppMenu.Registrar";
static const QString REGISTRAR_INTERFACE = "com.canonical.AppMenu.Registrar";
static const QString REGISTRAR_PATH = "/com/canonical/AppMenu/Registrar";

QDBusArgument &operator<<(QDBusArgument &argument, const MenuStruct &menuStruct)
{
    argument.beginStructure();
    argument << menuStruct.wId << menuStruct.service << menuStruct.path;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, MenuStruct &menuStruct)
{
    argument.beginStructure();
    argument >> menuStruct.wId >> menuStruct.service >> menuStruct.path;
    argument.endStructure();
    return argument;
}
QDebug operator<<(QDebug deg, const MenuStruct &menuStruct)
{
    qDebug() << "wId:" << menuStruct.wId << "server:" << menuStruct.service << "path:" << menuStruct.path;

    return deg;
}

MenuImporter *MenuImporter::instance() {
    static MenuImporter *ins = new MenuImporter();
    return ins;
}

MenuImporter::MenuImporter() : QObject()
, m_serviceWatcher(nullptr)
{
    qRegisterMetaType<WId>("WId");
    qDBusRegisterMetaType<MenuStruct>();
    qDBusRegisterMetaType<MenuList>();
}

bool MenuImporter::connectToBus()
{
    m_serviceWatcher = new QDBusServiceWatcher(this);
    m_serviceWatcher->setConnection(QDBusConnection::sessionBus());
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &MenuImporter::slotServiceUnregistered);
    connect(KWindowSystem::self(), &KWindowSystem::windowRemoved, this, &MenuImporter::UnregisterWindow);

    if (!QDBusConnection::sessionBus().registerService(REGISTRAR_INTERFACE))
        return false;

    new MenuImporterAdaptor(this);
    QDBusConnection::sessionBus().registerObject(REGISTRAR_PATH, this);

    return true;
}

void MenuImporter::RegisterWindow(WId id, const QDBusObjectPath& path)
{
    RegisterWindow(id, message().service(), path);
}

void MenuImporter::RegisterWindow(WId id, const QString &service, const QDBusObjectPath& path)
{
    if (path.path().isEmpty() || service.isEmpty()) return;

    if(m_menuServices.contains(id))
    {
        if(m_menuServices.value(id) == service && m_menuPaths.value(id) == path)
            return;

        UnregisterWindow(id);
    }

    m_menuServices.insert(id, service);
    m_menuPaths.insert(id, path);

    if (!m_serviceWatcher->watchedServices().contains(service))
        m_serviceWatcher->addWatchedService(service);

    emit WindowRegistered(id, service, path);
}

void MenuImporter::UnregisterWindow(WId id)
{
    if(m_menuServices.contains(id))
    {
        m_menuServices.remove(id);
        m_menuPaths.remove(id);

        emit WindowUnregistered(id);
    }
}

QString MenuImporter::GetMenuForWindow(WId id, QDBusObjectPath& path)
{
    path = m_menuPaths.value(id);
    return m_menuServices.value(id);
}

MenuList MenuImporter::GetMenus()
{
    MenuList list;
    foreach(WId id, m_menuServices.keys())
        list.append(MenuStruct(id, m_menuServices.value(id), m_menuPaths.value(id)));
    return list;
}

void MenuImporter::slotServiceUnregistered(const QString& service)
{
    QList<WId> ids = m_menuServices.keys(service);
    for(WId id : ids)
        UnregisterWindow(id);

    m_serviceWatcher->removeWatchedService(service);
}
