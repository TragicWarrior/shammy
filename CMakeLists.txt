cmake_minimum_required(VERSION 3.21)
project(shammy VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WITH_WEBENGINE "HTML/SVG artifact preview via Qt WebEngine" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Network Sql Svg)
find_package(Qt6 6.4 QUIET COMPONENTS Test)

set(HAVE_WEBENGINE OFF)
if(WITH_WEBENGINE)
    find_package(Qt6 6.4 COMPONENTS WebEngineQuick)
    if(TARGET Qt6::WebEngineQuick)
        set(HAVE_WEBENGINE ON)
    else()
        message(WARNING "Qt WebEngine not found; building without HTML artifact preview")
    endif()
endif()

qt_standard_project_setup()

set(CORE_SOURCES
    src/openai/SseParser.cpp
    src/openai/SseParser.h
    src/openai/ToolCallXml.h
    src/openai/OpenAiClient.cpp
    src/openai/OpenAiClient.h
    src/openai/ChatTypes.h
    src/openai/ModelCaps.cpp
    src/openai/ModelCaps.h
    src/persist/Store.cpp
    src/persist/Store.h
    src/artifacts/ContentSplitter.cpp
    src/artifacts/ContentSplitter.h
    src/artifacts/ArtifactExtractor.cpp
    src/artifacts/ArtifactExtractor.h
    src/artifacts/ArtifactMarkup.h
    src/artifacts/HtmlDocument.cpp
    src/artifacts/HtmlDocument.h
    src/artifacts/DocxExport.cpp
    src/artifacts/DocxExport.h
    src/artifacts/SpreadsheetExtract.cpp
    src/artifacts/SpreadsheetExtract.h
    src/mcp/JsonRpc.cpp
    src/mcp/JsonRpc.h
    src/mcp/McpClient.cpp
    src/mcp/McpClient.h
    src/mcp/McpHost.cpp
    src/mcp/McpHost.h
    src/mcp/McpConfig.cpp
    src/mcp/McpConfig.h
    src/compact/Compactor.cpp
    src/compact/Compactor.h
    src/websearch/WebSearch.cpp
    src/websearch/WebSearch.h
    src/Util.h
)

add_library(shammy_core STATIC ${CORE_SOURCES})
target_include_directories(shammy_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(shammy_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)
target_compile_definitions(shammy_core PUBLIC SHAMMY_VERSION="${PROJECT_VERSION}")
set_target_properties(shammy_core PROPERTIES AUTOMOC ON)

set(APP_SOURCES
    src/main.cpp
    src/controllers/ChatController.cpp
    src/controllers/ChatController.h
    src/controllers/SettingsController.cpp
    src/controllers/SettingsController.h
    src/controllers/ProjectController.cpp
    src/controllers/ProjectController.h
    src/controllers/McpController.cpp
    src/controllers/McpController.h
    src/models/MessageListModel.cpp
    src/models/MessageListModel.h
    src/models/ConversationListModel.cpp
    src/models/ConversationListModel.h
    src/models/ProjectListModel.cpp
    src/models/ProjectListModel.h
    src/models/SimpleListModels.h
    src/import/ClaudeCookies.cpp
    src/import/ClaudeCookies.h
    src/import/ClaudeImporter.cpp
    src/import/ClaudeImporter.h
)

set(QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/components/Sidebar.qml
    qml/components/ChatView.qml
    qml/components/MessageBubble.qml
    qml/components/InsightBlock.qml
    qml/components/Composer.qml
    qml/components/ArtifactPane.qml
    qml/components/ArtifactPopout.qml
    qml/components/ArtifactViewer.qml
    qml/components/SettingsToggleRow.qml
    qml/components/CodeBlock.qml
    qml/components/SelectableMarkdown.qml
    qml/components/TableBlock.qml
    qml/components/PermissionDialog.qml
    qml/components/SettingsSheet.qml
    qml/components/ThemedMenu.qml
    qml/components/ThemedMenuItem.qml
    qml/components/MenuIcon.qml
    qml/components/ProjectsOverview.qml
    qml/components/ProjectWorkspace.qml
)
if(HAVE_WEBENGINE)
    list(APPEND QML_FILES qml/components/ArtifactWebPreview.qml)
endif()

foreach(f ${QML_FILES})
    get_filename_component(_bn ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${_bn})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(shammy ${APP_SOURCES})
qt_add_qml_module(shammy
    URI Shammy
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${QML_FILES}
)

set(SHAMMY_ICON_PNGS
    resources/icons/shammy-16.png
    resources/icons/shammy-22.png
    resources/icons/shammy-24.png
    resources/icons/shammy-32.png
    resources/icons/shammy-48.png
    resources/icons/shammy-64.png
    resources/icons/shammy-128.png
    resources/icons/shammy-256.png
)
foreach(f ${SHAMMY_ICON_PNGS})
    get_filename_component(_bn ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${_bn})
endforeach()
qt_add_resources(shammy "app_icons"
    PREFIX "/icons"
    FILES ${SHAMMY_ICON_PNGS}
)
target_include_directories(shammy PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(shammy PRIVATE
    shammy_core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    Qt6::Network
    Qt6::Sql
)
if(HAVE_WEBENGINE)
    target_link_libraries(shammy PRIVATE Qt6::WebEngineQuick)
    target_compile_definitions(shammy PRIVATE SHAMMY_WEBENGINE)
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus)
    if(TARGET Qt6::DBus)
        target_link_libraries(shammy PRIVATE Qt6::DBus)
        target_compile_definitions(shammy PRIVATE SHAMMY_DBUS)
    endif()
endif()
find_package(OpenSSL)
if(OpenSSL_FOUND)
    target_link_libraries(shammy PRIVATE OpenSSL::Crypto)
    target_compile_definitions(shammy PRIVATE SHAMMY_OPENSSL)
endif()

include(GNUInstallDirs)
install(TARGETS shammy RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/linux/shammy.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/shammy.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES resources/icons/shammy-48.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps
        RENAME shammy.png)
install(FILES resources/icons/shammy-128.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
        RENAME shammy.png)

if(TARGET Qt6::Test)
    enable_testing()
    add_subdirectory(tests)
endif()
