/*!
 * This file is part of toolBLEx.
 * Copyright (c) 2022 Emeric Grange - All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * \date      2022
 * \author    Emeric Grange <emeric.grange@gmail.com>
 */

#include "DatabaseManager.h"
#include "VendorsDatabase.h"

#include "SettingsManager.h"
#include "MenubarManager.h"
#include "DeviceManager.h"
#include "ubertooth.h"

#include "utils_app.h"
#include "utils_screen.h"
#include "utils_sysinfo.h"
#include "utils_language.h"
#include "utils_clipboard.h"
#if defined(Q_OS_MACOS)
#include "utils_os_macos_dock.h"
#endif

#include <SingleApplication.h>

#include <QtGlobal>
#include <QLibraryInfo>
#include <QVersionNumber>

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include <QBluetoothLocalDevice>
#include <QBluetoothHostInfo>
#include <QTextStream>

#include <QApplication>
#include <QInputDialog>

/* ************************************************************************** */

QString promptForAdapterName()
{
    QList<QBluetoothHostInfo> adapters = QBluetoothLocalDevice::allDevices();
    QTextStream cin(stdin), cout(stdout);

    cout << "Available Bluetooth adapters:\n";
    for (int i = 0; i < adapters.size(); ++i) {
        cout << i << ": " << adapters[i].name() << " (" << adapters[i].address().toString() << ")\n";
    }
    cout << "Select adapter index: " << flush;
    int idx = -1;
    cin >> idx;
    if (idx >= 0 && idx < adapters.size())
        return adapters[idx].name();
    return QString();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Temporary DeviceManager to get adapter names
    DeviceManager tempManager;
    QStringList adapterNames = tempManager.getAvailableAdapterNames();

    QString selectedAdapter = QInputDialog::getItem(
        nullptr,
        "Select Bluetooth Adapter",
        "Adapter:",
        adapterNames,
        0,
        false
    );

    DeviceManager manager(false, selectedAdapter);

    // Hacks ///////////////////////////////////////////////////////////////////

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // NVIDIA suspend&resume hack
    auto format = QSurfaceFormat::defaultFormat();
    format.setOption(QSurfaceFormat::ResetNotification);
    QSurfaceFormat::setDefaultFormat(format);
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Qt 6.6+ mouse wheel hack
    qputenv("QT_QUICK_FLICKABLE_WHEEL_DECELERATION", "2500");
#endif

    // GUI application /////////////////////////////////////////////////////////

    SingleApplication app(argc, argv, true);

    // Application name
    app.setApplicationName("toolBLEx");
    app.setApplicationDisplayName("toolBLEx");
    app.setOrganizationName("toolBLEx");
    app.setOrganizationDomain("toolBLEx");

    // Application icon
    QIcon appIcon(":/assets/gfx/logos/icon.svg");
    app.setWindowIcon(appIcon);

    // Preload components
    VendorsDatabase::getInstance();
    DatabaseManager::getInstance();

    // Init components
    SettingsManager *sm = SettingsManager::getInstance();
    MenubarManager *mb = MenubarManager::getInstance();
    DeviceManager *dm = new DeviceManager;
    Ubertooth *uber = new Ubertooth;
    if (!sm ||!mb || !dm || !uber)
    {
        qWarning() << "Cannot init toolBLEx components!";
        return EXIT_FAILURE;
    }

    // Start scanning?
    if (sm->getScanAuto())
    {
        dm->scanDevices_start();
    }

    // Init generic utils
    UtilsApp *utilsApp = UtilsApp::getInstance();
    UtilsScreen *utilsScreen = UtilsScreen::getInstance();
    UtilsSysInfo *utilsSysInfo = UtilsSysInfo::getInstance();
    UtilsLanguage *utilsLanguage = UtilsLanguage::getInstance();
    UtilsClipboard *utilsClipboard = new UtilsClipboard();
    if (!utilsScreen || !utilsApp|| !utilsLanguage || !utilsClipboard)
    {
        qWarning() << "Cannot init toolBLEx utils!";
        return EXIT_FAILURE;
    }

    DeviceUtils::registerQML();

    // Translate the application
    utilsLanguage->setAppName("toolBLEx");
    utilsLanguage->loadLanguage("English");

    // ThemeEngine
    qmlRegisterSingletonType(QUrl("qrc:/qml/ThemeEngine.qml"), "ComponentLibrary", 1, 0, "Theme");

    // Then we start the UI
    QQmlApplicationEngine engine;
    QQmlContext *engine_context = engine.rootContext();

    engine_context->setContextProperty("settingsManager", sm);
    engine_context->setContextProperty("menubarManager", mb);
    engine_context->setContextProperty("deviceManager", dm);
    engine_context->setContextProperty("ubertooth", uber);

    engine_context->setContextProperty("utilsApp", utilsApp);
    engine_context->setContextProperty("utilsScreen", utilsScreen);
    engine_context->setContextProperty("utilsSysInfo", utilsSysInfo);
    engine_context->setContextProperty("utilsLanguage", utilsLanguage);
    engine_context->setContextProperty("utilsClipboard", utilsClipboard);

    // Load the main view
    engine.load(QUrl(QStringLiteral("qrc:/qml/DesktopApplication.qml")));

    if (engine.rootObjects().isEmpty())
    {
        qWarning() << "Cannot init QmlApplicationEngine!";
        return EXIT_FAILURE;
    }

    // For i18n retranslate
    utilsLanguage->setQmlEngine(&engine);

    // QQuickWindow must be valid at this point
    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));

    utilsApp->setQuickWindow(window); // to get additional infos

    // React to secondary instances
    QObject::connect(&app, &SingleApplication::instanceStarted, window, &QQuickWindow::show);
    QObject::connect(&app, &SingleApplication::instanceStarted, window, &QQuickWindow::raise);

#if defined(Q_OS_MACOS)
    // Menu bar
    mb->setupMenubar(window, dm);

    // Dock
    MacOSDockHandler *dockIconHandler = MacOSDockHandler::getInstance();
    dockIconHandler->setupDock(window);
    engine_context->setContextProperty("utilsDock", dockIconHandler);
#endif // Q_OS_MACOS

    return app.exec();
}

/* ************************************************************************** */
