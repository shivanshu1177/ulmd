cmake_minimum_required(VERSION 3.20)
project(ulmd VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ULMD_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(ULMD_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ULMD_ENABLE_LTO "Enable Link Time Optimization" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(ULMD_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(ULMD_WARNINGS_AS_ERRORS)
        list(APPEND ULMD_WARNING_FLAGS -Werror)
    endif()
endif()

if(ULMD_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(ULMD_ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

add_library(ulmd_interfaces INTERFACE)
target_include_directories(ulmd_interfaces INTERFACE include)
target_compile_features(ulmd_interfaces INTERFACE cxx_std_20)
target_compile_options(ulmd_interfaces INTERFACE ${ULMD_WARNING_FLAGS})

if(APPLE)
    target_compile_definitions(ulmd_interfaces INTERFACE ULMD_MACOS=1)
elseif(UNIX)
    target_compile_definitions(ulmd_interfaces INTERFACE ULMD_LINUX=1)
    target_link_libraries(ulmd_interfaces INTERFACE pthread)
endif()

set(ULMD_APPS ingress parser worker_risk ringctl ulmdsim tsc_cal)

file(GLOB ULMD_SOURCES src/*.cpp)
add_library(ulmd_lib STATIC ${ULMD_SOURCES})
target_link_libraries(ulmd_lib PUBLIC ulmd_interfaces)

foreach(app ${ULMD_APPS})
    add_executable(${app} apps/${app}.cpp)
    target_link_libraries(${app} PRIVATE ulmd_lib)
endforeach()


install(TARGETS ${ULMD_APPS} DESTINATION bin)