cmake_minimum_required(VERSION 3.17)
project(nimble_steps C)

set(CMAKE_C_STANDARD 99)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(isDebug TRUE)
else ()
    message("no debug")
    set(isDebug FALSE)
endif ()


if (isDebug)
    message("Debug build detected")
    add_compile_definitions(CONFIGURATION_DEBUG TYRAN_CONFIGURATION_DEBUG)
    set(CONFIGURATION_DEBUG 1)
endif ()


set(deps ../../deps/)

file(GLOB_RECURSE deps_src FOLLOW_SYMLINKS
        "${deps}*/*/src/lib/*.c"
        )

file(GLOB_RECURSE lib_src FOLLOW_SYMLINKS
        "*.c"
        )

add_library(nimble_steps
        ${deps_src}
        ${lib_src}

        )

target_include_directories(nimble_steps PUBLIC ${deps}piot/clog/src/include)
target_include_directories(nimble_steps PUBLIC ${deps}piot/discoid-c/src/include)
target_include_directories(nimble_steps PUBLIC ${deps}piot/tiny-libc/src/include)
target_include_directories(nimble_steps PUBLIC ${deps}piot/flood-c/src/include)
target_include_directories(nimble_steps PUBLIC ${deps}piot/imprint/src/include)
target_include_directories(nimble_steps PUBLIC ../include)

target_link_libraries(nimble_steps m)
