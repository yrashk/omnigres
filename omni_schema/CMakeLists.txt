cmake_minimum_required(VERSION 3.22)
project(omnischema_exe)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../cmake)

include(CPM)
include(cmake/SWIprolog.cmake)
include(cmake/Logtalk.cmake)

# FIXME: the capability to compile everything statically is currently problematic:
# as long as we don't need foreign libraries, we're fine, # but the moment we do,
# these static builds don't work.
#find_library(LIBSWIPL_STATIC NAMES swipl_static PATHS "${PLLIBDIR}")
unset(LIBSWIPL_STATIC)
find_library(LIBSWIPL NAMES swipl PATHS "${PLLIBDIR}")

if(LIBSWIPL_STATIC)
    set(STATIC TRUE)
    set(libswipl swipl_static)
else()
    set(libswipl swipl)
endif()

add_executable(omnischema_exe main.c ${CMAKE_CURRENT_BINARY_DIR}/app/application)
set_target_properties(omnischema_exe PROPERTIES OUTPUT_NAME "omni_schema")
target_link_libraries(omnischema_exe PRIVATE ${libswipl})
target_compile_definitions(omnischema_exe PRIVATE __SWI_PROLOG __SWI_EMBEDDED_)
target_include_directories(omnischema_exe PRIVATE "${PLBASE}/include")
target_link_directories(omnischema_exe PRIVATE "${PLLIBDIR}")

set_target_properties(omnischema_exe PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
if(APPLE)
    set_target_properties(omnischema_exe PROPERTIES INSTALL_RPATH "@loader_path")
else()
    set_target_properties(omnischema_exe PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

if(STATIC)
    if(APPLE)
        find_library(CoreFoundation NAMES CoreFoundation)
        target_link_libraries(omnischema_exe PRIVATE ${CoreFoundation})
        set_target_properties(omnischema_exe PROPERTIES ENABLE_EXPORTS ON)
    endif()

    find_library(libncurses NAMES ncurses REQUIRED)
    find_library(libz NAMES z REQUIRED)
    find_library(libm NAMES m REQUIRED)

    target_link_libraries(omnischema_exe PRIVATE ${libncurses} ${libz} ${libm})
endif()

set(omnischema_exe_SOURCES loader.lgt)

add_custom_command(
        DEPENDS ${omnischema_exe_SOURCES} settings-build.lgt
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/app/application
        COMMAND LOGTALKUSER=${Logtalk_SOURCE_DIR} LOGTALKHOME=${Logtalk_SOURCE_DIR} PATH=${Logtalk_SOURCE_DIR}/integration:$ENV{PATH}
        ${Logtalk_SOURCE_DIR}/scripts/embedding/swipl/swipl_logtalk_qlf.sh
        -f save
        -d ${CMAKE_CURRENT_BINARY_DIR}/app
        -s ${CMAKE_CURRENT_SOURCE_DIR}/settings-build.lgt
        -l ${CMAKE_CURRENT_SOURCE_DIR}/loader.lgt
        -c -x
        # Trigger a rebuild for omnischema_exe. Not pretty but does the job
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_SOURCE_DIR}/main.c)


add_custom_command(TARGET omnischema_exe POST_BUILD
        COMMAND cat $<TARGET_FILE:omnischema_exe> ${CMAKE_CURRENT_BINARY_DIR}/app/application > ${CMAKE_CURRENT_BINARY_DIR}/omnischema_exe1
        COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/omnischema_exe1 $<TARGET_FILE:omnischema_exe>
        COMMAND chmod +x $<TARGET_FILE:omnischema_exe>)

if(NOT STATIC)
    if(IS_SYMLINK ${LIBSWIPL})
        file(READ_SYMLINK ${LIBSWIPL} LIBSWIPL_)
        get_filename_component(LIBSWIPL_ ${LIBSWIPL_} NAME)
    else()
        get_filename_component(LIBSWIPL_ ${LIBSWIPL} NAME)
    endif()
    add_custom_target(copy-${LIBSWIPL_} BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${LIBSWIPL_}
            COMMAND ${CMAKE_COMMAND} -E copy ${LIBSWIPL} ${CMAKE_CURRENT_BINARY_DIR}/${LIBSWIPL_}
            DEPENDS ${LIBSWIPL})
    add_dependencies(omnischema_exe copy-${LIBSWIPL_})
endif()

enable_testing()
add_test(NAME omnischema_exe_test COMMAND ${Logtalk_SOURCE_DIR}/scripts/logtalk_tester.sh -p swi -l 1
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})