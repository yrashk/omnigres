cmake_minimum_required(VERSION 3.25.1)
project(omni_guard)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

include(CPM)
include(CTest)

enable_testing()

find_package(PostgreSQL REQUIRED)

CPMAddPackage(NAME pfr VERSION ${pfr_VERSION} SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../deps/pfr)
CPMAddPackage(NAME magic_enum VERSION ${pfr_VERSION} SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../deps/magic_enum)

add_executable(pg_nodetypes pg_nodetypes.cpp)
set_property(TARGET pg_nodetypes PROPERTY CXX_STANDARD 20)
target_link_libraries(pg_nodetypes magic_enum)
target_compile_options(pg_nodetypes PRIVATE "-fbracket-depth=512")
target_include_directories(pg_nodetypes PRIVATE ${PostgreSQL_SERVER_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/pg_nodetypes.hpp
        D
        COMMAND pg_nodetypes > ${CMAKE_BINARY_DIR}/pg_nodetypes.hpp
        DEPENDS pg_nodetypes.cpp
)
add_custom_target(pg_nodetypes_header ALL
        DEPENDS ${CMAKE_BINARY_DIR}/pg_nodetypes.hpp
)

add_postgresql_extension(
        omni_guard
        SCHEMA omni_guard
        RELOCATABLE false
        SOURCES omni_guard.c guard.cpp
        DEPENDS_ON omni libomni
)
set_property(TARGET omni_guard PROPERTY CXX_STANDARD 20)
target_include_directories(omni_guard PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(omni_guard libomni boost_pfr magic_enum)
target_compile_options(omni_guard PRIVATE "-fbracket-depth=512")
add_dependencies(omni_guard pg_nodetypes_header)

