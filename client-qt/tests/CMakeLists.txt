# Client-side tests.
#
# qt_add_qml_module attaches the QML module to driscord_client itself, so there
# is no library for tests to link against. A test therefore either compiles the
# unit under test straight into itself — the convention core/tests already uses
# for self-contained components — or exercises QML files that do not depend on
# the application module.
#
# Run them through the client-tests preset: it sets QT_QPA_PLATFORM=offscreen,
# without which the QML test needs an X server.

find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

if(MSVC)
    set(CLIENT_TEST_WARNING_FLAGS /W4)
else()
    set(CLIENT_TEST_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

add_executable(test_app_config
    test_app_config.cpp
    ../src/app/AppConfig.cpp
)
target_include_directories(test_app_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(test_app_config PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_app_config PRIVATE Qt6::Test)
add_test(NAME test_app_config COMMAND test_app_config)

add_executable(test_session_store
    test_session_store.cpp
    ../src/api/SessionStore.cpp
)
target_include_directories(test_session_store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(test_session_store PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_session_store PRIVATE Qt6::Test)
add_test(NAME test_session_store COMMAND test_session_store)

# The HTTP-facing tests talk to an in-process QTcpServer fixture
# (support/http_fixture.h) instead of mocking QNetworkReply.
add_executable(test_api_client
    test_api_client.cpp
    ../src/api/ApiClient.cpp
)
target_include_directories(test_api_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(test_api_client PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_api_client PRIVATE Qt6::Test Qt6::Network)
add_test(NAME test_api_client COMMAND test_api_client)

add_executable(test_auth_manager
    test_auth_manager.cpp
    ../src/api/ApiClient.cpp
    ../src/api/AuthManager.cpp
    ../src/api/SessionStore.cpp
)
target_include_directories(test_auth_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(test_auth_manager PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_auth_manager PRIVATE Qt6::Test Qt6::Network)
add_test(NAME test_auth_manager COMMAND test_auth_manager)

add_test(NAME client_version COMMAND driscord_client --version)
set_tests_properties(client_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^Driscord [0-9]+\\.[0-9]+\\.[0-9]+"
)

add_test(NAME client_help COMMAND driscord_client --help)
set_tests_properties(client_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: driscord \\[options\\]"
)

# QUICK_TEST_SOURCE_DIR is the directory quick_test_main scans for tst_*.qml.
add_executable(tst_qml_components tst_qml_components.cpp)
target_compile_options(tst_qml_components PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_compile_definitions(tst_qml_components PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(tst_qml_components PRIVATE Qt6::QuickTest Qt6::Quick)
add_test(NAME tst_qml_components COMMAND tst_qml_components)
set_tests_properties(tst_qml_components PROPERTIES
    LABELS "client;ui"
    TIMEOUT 30
)
