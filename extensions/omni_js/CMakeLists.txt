cmake_minimum_required(VERSION 3.25.1)
project(omni_js)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../cmake)
include(OpenSSL)

include(CTest)
include(FindPkgConfig)
include(CPM)

enable_testing()

find_package(PostgreSQL REQUIRED)

find_library(JSC JavaScriptCore)
IF(NOT JSC)
    pkg_search_module(JSC REQUIRED javascriptcoregtk-4.0)
endif()


add_postgresql_extension(
        omni_js
        VERSION 0.1
        SCHEMA omni_js
        RELOCATABLE false
        SCRIPTS omni_js--0.1.sql
        SOURCES omni_js.c)

if(DEFINED JSC_INCLUDE_DIRS)
    foreach(dir ${JSC_INCLUDE_DIRS})
    message(STATUS ${dir})
    endforeach()
    target_include_directories(omni_js PRIVATE ${JSC_INCLUDE_DIRS})
endif()
if(DEFINED JSC_LIBRARY_DIRS)
    target_link_directories(omni_js PRIVATE ${JSC_LIBRARY_DIRS})
endif()
if(DEFINED JSC_LIBRARIES)
    target_link_libraries(omni_js ${JSC_LIBRARIES})
else()
    target_link_libraries(omni_js ${JSC})
endif()