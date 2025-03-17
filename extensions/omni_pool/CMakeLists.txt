cmake_minimum_required(VERSION 3.25.1)
project(omni_pool)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.1)
        message(FATAL_ERROR "GCC version must be at least 13.1 Detected version: ${CMAKE_CXX_COMPILER_VERSION}")
    endif ()
endif ()

include(CTest)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

enable_testing()

find_package(PostgreSQL REQUIRED)

add_postgresql_extension(
        omni_pool
        SOURCES omni_pool.cpp
        SCHEMA omni_pool
        DEPENDS_ON omni libomni
        RELOCATABLE false)

target_compile_features(omni_pool PUBLIC cxx_std_20)
target_link_libraries(omni_pool libomni)

target_include_directories(omni_pool PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/deps
        ${CMAKE_CURRENT_LIST_DIR}/../../deps/boost_1_87_0
        ${CMAKE_CURRENT_LIST_DIR}/../../deps/fmt/include)
