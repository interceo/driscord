
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

add_executable(test_update_manifest
    test_update_manifest.cpp
    ../src/update/UpdateManifest.cpp
)
target_include_directories(test_update_manifest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_definitions(test_update_manifest PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_compile_options(test_update_manifest PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_update_manifest PRIVATE Qt6::Test)
add_test(NAME test_update_manifest COMMAND test_update_manifest)

add_executable(test_minisign
    test_minisign.cpp
    ../src/update/Minisign.cpp
)
target_include_directories(test_minisign PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_minisign PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_compile_options(test_minisign PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_minisign PRIVATE Qt6::Test
    driscord::google_webrtc driscord::boringssl_headers)
add_test(NAME test_minisign COMMAND test_minisign)

add_executable(test_install_swap
    test_install_swap.cpp
    ../src/update/InstallSwap.cpp
    ../src/update/InstallLayout.cpp
)
target_include_directories(test_install_swap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(test_install_swap PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_install_swap PRIVATE Qt6::Test)
add_test(NAME test_install_swap COMMAND test_install_swap)

add_executable(test_update_manager
    test_update_manager.cpp
    ../src/update/UpdateManager.cpp
    ../src/update/UpdateManifest.cpp
    ../src/update/Minisign.cpp
    ../src/update/InstallLayout.cpp
    ../src/update/InstallSwap.cpp
)
target_include_directories(test_update_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_update_manager PRIVATE ${CLIENT_TEST_WARNING_FLAGS})
target_link_libraries(test_update_manager PRIVATE Qt6::Test Qt6::Network
    driscord::google_webrtc driscord::boringssl_headers)
add_test(NAME test_update_manager COMMAND test_update_manager)

add_test(NAME client_version COMMAND driscord_client --version)
set_tests_properties(client_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^Driscord [0-9]+\\.[0-9]+\\.[0-9]+"
)

add_test(NAME client_help COMMAND driscord_client --help)
set_tests_properties(client_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: driscord \\[options\\]"
)

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
