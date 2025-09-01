cmake_minimum_required(VERSION 3.25.1)
project(omni_livehtml)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.1)
        message(FATAL_ERROR "GCC version must be at least 13.1 Detected version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif ()
endif ()

include(CTest)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

enable_testing()

find_package(PostgreSQL REQUIRED)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/dependencies/versions.cmake)
include(CheckCSourceCompiles)

CPMAddPackage(NAME omni_livehtml_lexbor SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/deps/lexbor VERSION ${VERSION_../extensions/omni_livehtml/deps/lexbor}
        OPTIONS "LEXBOR_BUILD_SHARED OFF" "LEXBOR_BUILD_SEPARATELY ON" "LEXBOR_BUILD_TESTS_CPP OFF" "LEXBOR_INSTALL_HEADERS")
#set_property(TARGET lexbor PROPERTY POSITION_INDEPENDENT_CODE ON)

add_postgresql_extension(
        omni_livehtml
        COMMENT "HTML toolkit"
        SOURCES omni_livehtml.cpp
        SCHEMA omni_livehtml
        DEPENDS_ON lexbor-core_static lexbor-html_static lexbor-dom_static
        RELOCATABLE false)

target_link_libraries(omni_livehtml PRIVATE lexbor-core_static lexbor-html_static lexbor-dom_static)
target_compile_features(omni_livehtml PUBLIC cxx_std_20)
target_include_directories(omni_livehtml PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/deps
        ${CMAKE_CURRENT_LIST_DIR}/deps/lexbor/source
        ${CMAKE_CURRENT_LIST_DIR}/../../deps/fmt/include)
