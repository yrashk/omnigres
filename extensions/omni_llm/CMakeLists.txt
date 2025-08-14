cmake_minimum_required(VERSION 3.25.1)
project(omni_llm)

include(CTest)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../cmake)

enable_testing()

find_package(PostgreSQL REQUIRED)

add_postgresql_extension(
        omni_llm
        COMMENT "LLM toolkit"
        SCHEMA omni_llm
        REQUIRES omni_httpc omni_worker omni_polyfill omni_id
        RELOCATABLE false)
