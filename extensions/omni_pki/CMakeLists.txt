cmake_minimum_required(VERSION 3.25.1)
project(omni_pki)

include(CTest)
include(FindPkgConfig)
include(OpenSSL)

find_package(OpenSSL REQUIRED)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../cmake)

enable_testing()

add_postgresql_extension(
        omni_pki
        VERSION 0.1
        SCHEMA omni_pki
        RELOCATABLE false
        SCRIPTS omni_pki--0.1.sql
        SOURCES omni_pki.c)

target_link_libraries(omni_pki libpgaug OpenSSL::SSL)