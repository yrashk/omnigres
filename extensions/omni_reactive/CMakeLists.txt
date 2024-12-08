cmake_minimum_required(VERSION 3.25.1)
project(omni_id)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

include(CPM)
include(CTest)

enable_testing()

find_package(PostgreSQL REQUIRED)

add_postgresql_extension(
        omni_reactive
        SCHEMA omni_reactive
        SOURCES omni_reactive.c
        REQUIRES omni_sql
        DEPENDS_ON omni
)

target_link_libraries(omni_reactive libomni)