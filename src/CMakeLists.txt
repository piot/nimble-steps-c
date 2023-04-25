cmake_minimum_required(VERSION 3.17)
project(nimble_steps C)

set(CMAKE_C_STANDARD 99)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(isDebug TRUE)
else()
    message("no debug")
    set(isDebug FALSE)
endif()


if (isDebug)
        message("Debug build detected")
        add_compile_definitions(CONFIGURATION_DEBUG TYRAN_CONFIGURATION_DEBUG)
        set(CONFIGURATION_DEBUG 1)
endif()


set(deps ../deps/)


include_directories(${deps}piot/clog/src/include)
include_directories(${deps}piot/discoid-c/src/include)
include_directories(${deps}piot/tiny-libc/src/include)
include_directories(${deps}piot/flood-c/src/include)
include_directories(${deps}piot/imprint/src/include)
include_directories(include)

file(GLOB_RECURSE deps_src FOLLOW_SYMLINKS
        "${deps}*/*/src/lib/*.c"
        )

file(GLOB_RECURSE lib_src FOLLOW_SYMLINKS
        "lib/*.c"
        )

add_executable(nimble_steps_example
    ${deps_src}
    ${lib_src}
    examples/main.c
)
target_link_libraries(nimble_steps_example m)
