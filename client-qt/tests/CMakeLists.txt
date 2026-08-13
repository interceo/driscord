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
