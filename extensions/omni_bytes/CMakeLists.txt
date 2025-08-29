cmake_minimum_required(VERSION 3.25.1)
project(omni_bytes)

include(CTest)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

enable_testing()

find_package(PostgreSQL REQUIRED)

add_postgresql_extension(
        omni_bytes
        COMMENT "Byte and binary data handling"
        SCHEMA omni_bytes
        RELOCATABLE false)
