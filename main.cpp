#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include "menuproxy.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    qputenv("QT_QPA_PLATFORM", "xcb");

    QGuiApplication::setDesktopSettingsAware(false);
//    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);

    app.setQuitOnLastWindowClosed(false);

    MenuProxy proxy;
    
    // auto *menuimporter = new MenuImporter(&app);
    // menuimporter->connectToBus();

    return app.exec();
}
