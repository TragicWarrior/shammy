function(shammy_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE shammy_core Qt6::Test Qt6::Network Qt6::Sql)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
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

# Controller-level test: the controllers and list models live in the app target
# (next to the QML module), so build the few it needs directly into this test.
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
add_executable(test_chat_controller test_chat_controller.cpp ${CONTROLLER_TEST_SOURCES})
target_link_libraries(test_chat_controller PRIVATE shammy_core Qt6::Test Qt6::Network Qt6::Sql)
target_include_directories(test_chat_controller PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_chat_controller COMMAND test_chat_controller)
