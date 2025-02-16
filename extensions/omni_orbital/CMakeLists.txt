cmake_minimum_required(VERSION 3.25.1)
project(omni_orbital)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

include(CPM)
include(CTest)

enable_testing()

find_package(PostgreSQL REQUIRED)

add_postgresql_extension(
        omni_orbital
        COMMENT "REST API toolkit (with PostgREST support)"
        SCHEMA omni_orbital
        REQUIRES omni_httpd omni_web omni_vfs omni_mimetypes
)