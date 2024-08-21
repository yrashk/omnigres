include(CPM)
include(CheckCSourceCompiles)

set(_h2o_options)
list(APPEND _h2o_options "WITH_MRUBY OFF")
list(APPEND _h2o_options "DISABLE_LIBUV ON")
# This is to make h2o find WSLAY
list(APPEND _h2o_options "CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}")

find_package(WSLAY REQUIRED)

cmake_policy(SET CMP0042 NEW)
CPMAddPackage(NAME h2o GIT_REPOSITORY https://github.com/h2o/h2o GIT_TAG 579135a VERSION 2.3.0-579135a OPTIONS "${_h2o_options}")
set_property(TARGET libh2o-evloop PROPERTY POSITION_INDEPENDENT_CODE ON)

add_dependencies(libh2o wslay)
add_dependencies(libh2o-evloop wslay)