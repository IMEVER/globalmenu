cmake_minimum_required(VERSION 3.13)

set(PROJECT globalmenu)
project(${PROJECT})

# remove_definitions(-DQT_NO_CAST_FROM_ASCII -DQT_STRICT_ITERATORS -DQT_NO_CAST_FROM_BYTEARRAY -DQT_NO_KEYWORDS)

set(CMAKE_CXX_STANDARD 14)
# 启用 qt moc 的支持
set(CMAKE_AUTOMOC ON)
# 启用 qrc 资源文件的支持
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5Widgets REQUIRED)
find_package(Qt5DBus REQUIRED)
find_package(KF5WindowSystem REQUIRED)
find_package(Qt5X11Extras REQUIRED)

set(SRCS
        menuimporter.h menuimporter.cpp
        # dbusmenutypes_p.h dbusmenutypes_p.cpp
        # dbusmenushortcut_p.h dbusmenushortcut_p.cpp
        gdbusmenutypes_p.h gdbusmenutypes_p.cpp
        # gmenu dbus proxy
        utils.h utils.cpp
        window.h window.cpp
        menuproxy.h menuproxy.cpp
        menu.h menu.cpp
        icons.h icons.cpp
        actions.h actions.cpp
        )
qt5_add_dbus_adaptor(SRCS com.canonical.AppMenu.Registrar.xml menuimporter.h MenuImporter menuimporteradaptor MenuImporterAdaptor)
qt5_add_dbus_adaptor(SRCS com.canonical.dbusmenu.xml window.h Window)

add_library(${PROJECT} STATIC ${SRCS})

target_include_directories(${PROJECT} PUBLIC
        ${Qt5Widgets_INCLUDE_DIRS}
        ${Qt5DBus_INCLUDE_DIRS}
        ${Qt5X11Extras_INCLUDE_DIRS}
        )

target_link_libraries(${PROJECT}
        Qt5::Widgets
        Qt5::DBus
        Qt5::X11Extras
        KF5::WindowSystem
        xcb
        )
