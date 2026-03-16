add_library(driscord_c SHARED
    bindings/c_api/driscord.cpp
)

target_include_directories(driscord_c PUBLIC
    bindings/c_api
)

target_link_libraries(driscord_c
    driscord_core
)