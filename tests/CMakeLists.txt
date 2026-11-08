# Unit-test binaries. The default build does not compile them.
# `cmake --build <dir> --target tests` (or `make tests`) does.
add_custom_target(tests)

function(shammy_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE shammy_core Qt6::Test Qt6::Network Qt6::Sql)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(tests ${name})
endfunction()

shammy_test(test_sseparser)
shammy_test(test_artifacts)
shammy_test(test_mcp_framing)
shammy_test(test_store)
shammy_test(test_openai_request)
shammy_test(test_compactor)
shammy_test(test_model_caps)
shammy_test(test_mcp_config)
shammy_test(test_websearch)
shammy_test(test_reply)
shammy_test(test_toolcall_xml)
shammy_test(test_docx_export)
shammy_test(test_spreadsheet_extract)
shammy_test(test_document_extract)
shammy_test(test_attach)
shammy_test(test_mcp_client)
shammy_test(test_web_fetch)
shammy_test(test_pdf_extract)
shammy_test(test_claude_import)
shammy_test(test_skills)
shammy_test(test_shell_command)
shammy_test(test_ollama_native)
shammy_test(test_image_gen)
add_executable(test_image_viewer test_image_viewer.cpp)
target_link_libraries(test_image_viewer PRIVATE shammy_core Qt6::Test Qt6::Network Qt6::Sql Qt6::Quick)
target_include_directories(test_image_viewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_image_viewer PRIVATE
    QML_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_image_viewer COMMAND test_image_viewer)
add_dependencies(tests test_image_viewer)
# Opens real windows: keep them off the user's display.
set_tests_properties(test_image_viewer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_message_bubble test_message_bubble.cpp)
target_link_libraries(test_message_bubble PRIVATE shammy_core Qt6::Test Qt6::Quick)
target_include_directories(test_message_bubble PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_message_bubble PRIVATE
    QML_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_message_bubble COMMAND test_message_bubble)
add_dependencies(tests test_message_bubble)
# Loads MessageBubble off the user's display.
set_tests_properties(test_message_bubble PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_sidebar test_sidebar.cpp)
target_link_libraries(test_sidebar PRIVATE shammy_core Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_include_directories(test_sidebar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_sidebar PRIVATE
    QML_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_sidebar COMMAND test_sidebar)
add_dependencies(tests test_sidebar)
# Loads Sidebar off the user's display.
set_tests_properties(test_sidebar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_artifact_viewer test_artifact_viewer.cpp)
target_link_libraries(test_artifact_viewer PRIVATE
    shammy_core Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_include_directories(test_artifact_viewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_artifact_viewer PRIVATE
    QML_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_artifact_viewer COMMAND test_artifact_viewer)
add_dependencies(tests test_artifact_viewer)
# Loads ArtifactViewer off the user's display.
set_tests_properties(test_artifact_viewer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_chat_scroll test_chat_scroll.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/ChatListWheel.cpp)
target_link_libraries(test_chat_scroll PRIVATE shammy_core Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_include_directories(test_chat_scroll PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_chat_scroll PRIVATE
    QML_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_chat_scroll COMMAND test_chat_scroll)
add_dependencies(tests test_chat_scroll)
# Loads ChatView off the user's display.
set_tests_properties(test_chat_scroll PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Controller-level tests: the controllers and list models live in the app target
# (next to the QML module), so build the few they need directly into each test.
set(CONTROLLER_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/controllers/ChatController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/ChatController.h
    ${CMAKE_SOURCE_DIR}/src/controllers/SettingsController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/SettingsController.h
    ${CMAKE_SOURCE_DIR}/src/controllers/ProjectController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/ProjectController.h
    ${CMAKE_SOURCE_DIR}/src/controllers/McpController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/McpController.h
    ${CMAKE_SOURCE_DIR}/src/models/MessageListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/MessageListModel.h
    ${CMAKE_SOURCE_DIR}/src/models/ConversationListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/ConversationListModel.h
    ${CMAKE_SOURCE_DIR}/src/models/ProjectListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/ProjectListModel.h
    ${CMAKE_SOURCE_DIR}/src/models/SimpleListModels.h
)
function(shammy_controller_test name)
    add_executable(${name} ${name}.cpp ${CONTROLLER_TEST_SOURCES})
    target_link_libraries(${name} PRIVATE shammy_core Qt6::Test Qt6::Network Qt6::Sql)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(tests ${name})
endfunction()
shammy_controller_test(test_chat_controller)
shammy_controller_test(test_project_files)
