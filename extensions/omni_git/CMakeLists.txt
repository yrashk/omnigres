cmake_minimum_required(VERSION 3.25.1)
project(omni_git)

include(CTest)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../cmake)

include(CPM)

enable_testing()

find_package(PostgreSQL REQUIRED)

CPMAddPackage(NAME libgit2 GIT_REPOSITORY https://github.com/libgit2/libgit2 GIT_TAG v1.5.1 VERSION 1.5.1
        OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_TESTS OFF" "BUILD_CLI OFF" "CMAKE_POSITION_INDEPENDENT_CODE ON")

add_postgresql_extension(
        omni_git
        VERSION 0.1
        SCHEMA omni_git
        RELOCATABLE false
        SCRIPTS omni_git--0.1.sql
        SOURCES alloc.c config.c omni_git.c)

target_link_libraries(omni_git libgit2package)
target_include_directories(omni_git PRIVATE "${libgit2_SOURCE_DIR}/include")