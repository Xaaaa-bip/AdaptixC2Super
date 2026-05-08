cmake_minimum_required(VERSION 3.28) # 3.15
project(AdaptixClient LANGUAGES C CXX)

option(ADAPTIX_STATIC_QT
    "Link against a static Qt build (Windows: one exe, no Qt DLLs; you must install/build static Qt and set CMAKE_PREFIX_PATH)"
    OFF)
if(ADAPTIX_STATIC_QT)
    add_compile_definitions(QT_STATIC)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    if(NOT Qt6_DIR AND DEFINED ENV{Qt6_DIR} AND NOT "$ENV{Qt6_DIR}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{Qt6_DIR}" Qt6_DIR)
    endif()
    if(NOT Qt6_DIR)
        set(Qt6_DIR "C:/msys64/mingw64/lib/cmake" CACHE PATH "Qt6 CMake dir (parent of Qt6/Qt6Config.cmake)")
    endif()
endif()
set(QT_NO_PRIVATE_MODULE_WARNING ON)

add_subdirectory(Libs/kddockwidgets)
add_subdirectory(Libs/qlementine)

if(WIN32 AND MINGW)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()
find_package( OpenSSL REQUIRED )

if(WIN32 AND MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-subsystem,windows")
endif()

find_package(Qt6
        REQUIRED COMPONENTS
        Core
        Gui
        Widgets
        Network
        WebSockets
        Sql
        Qml
        Svg
)

include_directories(
        Headers
        Libs
)

### KDDOCKWIDGETS

set(KDDOCKWIDGETS_RESOURCES Libs/kddockwidgets/kddockwidgets_resources.qrc)

### KONSOLE

set(KONSOLE_SOURCES

        Libs/Konsole/res.qrc

        Libs/Konsole/util/utf8proc.c
        Libs/Konsole/util/CharWidth.cpp
        Libs/Konsole/util/ColorScheme.cpp
        Libs/Konsole/util/Filter.cpp
        Libs/Konsole/util/History.cpp
        Libs/Konsole/util/HistorySearch.cpp
        Libs/Konsole/util/KeyboardTranslator.cpp
        Libs/Konsole/util/SearchBar.cpp
        Libs/Konsole/util/TerminalCharacterDecoder.cpp
        Libs/Konsole/Emulation.cpp
        Libs/Konsole/Vt102Emulation.cpp
        Libs/Konsole/Screen.cpp
        Libs/Konsole/ScreenWindow.cpp
        Libs/Konsole/TerminalDisplay.cpp
        Libs/Konsole/konsole.cpp
)

set_source_files_properties(
        Libs/Konsole/util/utf8proc.c
        PROPERTIES LANGUAGE C
)

set(KONSOLE_HEADERS
        Libs/Konsole/util/utf8proc.h
        Libs/Konsole/util/CharWidth.h
        Libs/Konsole/util/CharacterColor.h
        Libs/Konsole/util/Character.h
        Libs/Konsole/util/ColorScheme.h
        Libs/Konsole/util/Filter.h
        Libs/Konsole/util/History.h
        Libs/Konsole/util/HistorySearch.h
        Libs/Konsole/util/KeyboardTranslator.h
        Libs/Konsole/util/SearchBar.h
        Libs/Konsole/util/TerminalCharacterDecoder.h
        Libs/Konsole/Emulation.h
        Libs/Konsole/Vt102Emulation.h
        Libs/Konsole/Screen.h
        Libs/Konsole/ScreenWindow.h
        Libs/Konsole/TerminalDisplay.h
        Libs/Konsole/konsole.h
)

###

SET( HEADERS
        Headers/main.h
        Headers/Utils/Logs.h
        Headers/UI/Dialogs/DialogConnect.h
        Headers/Utils/Convert.h
        Headers/Utils/IpdbReader.h
        Headers/MainAdaptix.h
        Headers/Client/Storage.h
        Headers/Client/AuthProfile.h
        Headers/Utils/FileSystem.h
        Headers/Client/Requestor.h
        Headers/Client/HttpRequestManager.h
        Headers/UI/MainUI.h
        Headers/UI/Widgets/AdaptixWidget.h
        Headers/UI/Widgets/LogsWidget.h
        Headers/Workers/WebSocketWorker.h
        Headers/UI/Dialogs/DialogSyncPacket.h
        Headers/UI/Widgets/ListenersWidget.h
        Headers/UI/Dialogs/DialogListener.h
        Headers/UI/Widgets/SessionsTableWidget.h
        Headers/UI/Dialogs/DialogAgent.h
        Headers/UI/Dialogs/DialogWebDelivery.h
        Headers/UI/Widgets/ConsoleWidget.h
        Headers/Agent/Agent.h
        Headers/Workers/LastTickWorker.h
        Headers/Agent/Commander.h
        Headers/UI/Widgets/DownloadsWidget.h
        Headers/UI/Widgets/BrowserFilesWidget.h
        Headers/UI/Widgets/BrowserProcessWidget.h
        Headers/UI/Widgets/TasksWidget.h
        Headers/UI/Dialogs/DialogSettings.h
        Headers/Client/Extender.h
        Headers/UI/Dialogs/DialogExtender.h
        Headers/Utils/CustomElements.h
        Headers/Client/Settings.h
        Headers/Client/ConsoleTheme.h
        Headers/UI/Widgets/TunnelsWidget.h
        Headers/Utils/KeyPressHandler.h
        Headers/UI/Graph/SessionsGraph.h
        Headers/UI/Graph/GraphItem.h
        Headers/UI/Graph/GraphItemLink.h
        Headers/UI/Graph/GraphScene.h
        Headers/UI/Graph/LayoutTreeLeft.h
        Headers/UI/Graph/LayoutTreeTop.h
        Headers/UI/Widgets/ScreenshotsWidget.h
        Headers/UI/Dialogs/DialogTunnel.h
        Headers/Client/TunnelEndpoint.h
        Headers/Workers/TunnelWorker.h
        Headers/Workers/SocksHandshakeWorker.h
        Headers/UI/Widgets/TerminalContainerWidget.h
        Headers/Workers/TerminalWorker.h
        Headers/Workers/BuildWorker.h
        Headers/UI/Dialogs/DialogDownloader.h
        Headers/Workers/DownloaderWorker.h
        Headers/Workers/UploaderWorker.h
        Headers/UI/Dialogs/DialogUploader.h
        Headers/Client/AxScript/AxScriptManager.h
        Headers/Client/AxScript/AxScriptEngine.h
        Headers/Client/AxScript/AxScriptWorker.h
        Headers/Client/AxScript/AxUiFactory.h
        Headers/Client/AxScript/BridgeApp.h
        Headers/Client/AxScript/BridgeEvent.h
        Headers/Client/AxScript/BridgeForm.h
        Headers/Client/AxScript/BridgeMenu.h
        Headers/Client/AxScript/AxCommandWrappers.h
        Headers/Client/AxScript/AxElementWrappers.h
        Headers/UI/Widgets/CredentialsWidget.h
        Headers/UI/Dialogs/DialogCredential.h
        Headers/UI/Widgets/AxConsoleWidget.h
        Headers/UI/Dialogs/DialogTarget.h
        Headers/UI/Dialogs/DialogSaveTask.h
        Headers/UI/Widgets/ChatWidget.h
        Headers/UI/Widgets/AbstractDock.h
        Headers/Utils/NonBlockingDialogs.h
        Headers/UI/Dialogs/DialogSubscriptions.h
        Headers/Utils/FontManager.h
        Headers/Utils/TitleBarStyle.h
        Headers/UI/Widgets/DockWidgetRegister.h
)

add_executable(AdaptixClient
        ${KONSOLE_SOURCES}
        ${KONSOLE_HEADERS}

        ${KDDOCKWIDGETS_RESOURCES}

        ${HEADERS}

        Source/main.cpp

        Resources/Resources.qrc

        Source/Utils/Logs.cpp
        Source/UI/Dialogs/DialogConnect.cpp
        Source/UI/Dialogs/DialogSubscriptions.cpp
        Source/Utils/Convert.cpp
        Source/Utils/IpdbReader.cpp
        Source/MainAdaptix.cpp
        Source/Client/Storage.cpp
        Source/Client/AuthProfile.cpp
        Source/Utils/FileSystem.cpp
        Source/Client/Requestor.cpp
        Source/Client/HttpRequestManager.cpp
        Source/UI/MainUI.cpp
        Source/UI/Widgets/AdaptixWidget.cpp
        Source/UI/Widgets/LogsWidget.cpp
        Source/Workers/WebSocketWorker.cpp
        Source/UI/Dialogs/DialogSyncPacket.cpp
        Source/Client/ProcessSyncPacket.cpp
        Source/UI/Widgets/ListenersWidget.cpp
        Source/UI/Dialogs/DialogListener.cpp
        Source/UI/Widgets/SessionsTableWidget.cpp
        Source/UI/Dialogs/DialogAgent.cpp
        Source/UI/Dialogs/DialogWebDelivery.cpp
        Source/UI/Dialogs/DialogAgentData.cpp
        Source/UI/Widgets/ConsoleWidget.cpp
        Source/Agent/Agent.cpp
        Source/Workers/LastTickWorker.cpp
        Source/Agent/Commander.cpp
        Source/UI/Widgets/DownloadsWidget.cpp
        Source/UI/Widgets/BrowserFilesWidget.cpp
        Source/UI/Widgets/BrowserProcessWidget.cpp
        Source/UI/Widgets/TasksWidget.cpp
        Source/UI/Dialogs/DialogSettings.cpp
        Source/Client/Extender.cpp
        Source/UI/Dialogs/DialogExtender.cpp
        Source/Utils/CustomElements.cpp
        Source/Client/Settings.cpp
        Source/Client/ConsoleTheme.cpp
        Source/UI/Widgets/TunnelsWidget.cpp
        Source/UI/Graph/SessionsGraph.cpp
        Source/UI/Graph/GraphItem.cpp
        Source/UI/Graph/GraphItemLink.cpp
        Source/UI/Graph/GraphScene.cpp
        Source/UI/Graph/LayoutTreeLeft.cpp
        Source/UI/Graph/LayoutTreeTop.cpp
        Source/UI/Widgets/ScreenshotsWidget.cpp
        Source/UI/Dialogs/DialogTunnel.cpp
        Source/Client/TunnelEndpoint.cpp
        Source/Workers/TunnelWorker.cpp
        Source/Workers/SocksHandshakeWorker.cpp
        Source/UI/Widgets/TerminalContainerWidget.cpp
        Source/Workers/TerminalWorker.cpp
        Source/Workers/BuildWorker.cpp
        Source/UI/Dialogs/DialogDownloader.cpp
        Source/Workers/DownloaderWorker.cpp
        Source/Workers/UploaderWorker.cpp
        Source/UI/Dialogs/DialogUploader.cpp
        Source/Client/AxScript/AxScriptManager.cpp
        Source/Client/AxScript/AxScriptEngine.cpp
        Source/Client/AxScript/AxScriptWorker.cpp
        Source/Client/AxScript/AxUiFactory.cpp
        Source/Client/AxScript/BridgeApp.cpp
        Source/Client/AxScript/BridgeEvent.cpp
        Source/Client/AxScript/BridgeForm.cpp
        Source/Client/AxScript/BridgeMenu.cpp
        Source/Client/AxScript/AxCommandWrappers.cpp
        Source/Client/AxScript/AxElementWrappers.cpp
        Source/UI/Widgets/CredentialsWidget.cpp
        Source/UI/Dialogs/DialogCredential.cpp
        Source/UI/Widgets/AxConsoleWidget.cpp
        Source/UI/Widgets/TargetsWidget.cpp
        Source/UI/Dialogs/DialogTarget.cpp
        Source/UI/Dialogs/DialogSaveTask.cpp
        Source/UI/Widgets/ChatWidget.cpp
        Source/Utils/NonBlockingDialogs.cpp
        Source/Utils/FontManager.cpp
        Source/Utils/TitleBarStyle.cpp
)

set(ADAPTIX_QQWRY_IPDB "${CMAKE_CURRENT_SOURCE_DIR}/qqwry.ipdb")
if (EXISTS "${ADAPTIX_QQWRY_IPDB}")
    add_custom_command(TARGET AdaptixClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ADAPTIX_QQWRY_IPDB}"
            "$<TARGET_FILE_DIR:AdaptixClient>/qqwry.ipdb"
        COMMENT "Copy qqwry.ipdb beside AdaptixClient executable")
endif()

target_compile_definitions(
        AdaptixClient
        PUBLIC KDDW_FRONTEND_QTWIDGETS KDDW_FRONTEND_QT BUILDING_DOCKS_LIBRARY
        PRIVATE QT_DEPRECATED_WARNINGS QT_NO_KEYWORDS
#        QT_NO_SIGNALS_SLOTS_KEYWORDS
)

target_link_libraries( AdaptixClient
        PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::WebSockets
        Qt6::Sql
        Qt6::Qml
        OpenSSL::Crypto

        kddockwidgets
        qlementine
)

if(WIN32 AND ADAPTIX_STATIC_QT)
    set(_adaptix_static_plugins "")
    foreach(_p IN ITEMS
            Qt6::QWindowsIntegrationPlugin
            Qt6::QGifPlugin
            Qt6::QICOPlugin
            Qt6::QJpegPlugin
            Qt6::QSvgPlugin
            Qt6::QSQLiteDriverPlugin
            Qt6::QModernWindowsStylePlugin
            Qt6::QSchannelBackendPlugin
            Qt6::QCertOnlyBackendPlugin
    )
        if(TARGET ${_p})
            list(APPEND _adaptix_static_plugins ${_p})
        endif()
    endforeach()
    if(_adaptix_static_plugins)
        qt_import_plugins(AdaptixClient INCLUDE ${_adaptix_static_plugins})
    endif()
    if(COMMAND qt_import_qml_plugins)
        qt_import_qml_plugins(AdaptixClient)
    endif()
endif()

if(WIN32)
    target_link_libraries( AdaptixClient
            PRIVATE
            wsock32
            ws2_32
            crypt32
            iphlpapi
            netapi32
            version
            winmm
            userenv
            dwmapi
    )
    if(ADAPTIX_STATIC_QT AND MINGW)
        target_link_options(AdaptixClient PRIVATE -static)
    else()
        target_link_options( AdaptixClient PRIVATE -static-libgcc -static-libstdc++ )
        target_link_libraries( AdaptixClient PRIVATE -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic )
    endif()
elseif(UNIX)
    target_link_libraries( AdaptixClient
            PRIVATE
            pthread
            dl
    )
endif()