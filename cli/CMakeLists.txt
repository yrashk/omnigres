cmake_minimum_required(VERSION 3.22)
project(omnigres)

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

add_executable(omnigres main.c ${CMAKE_CURRENT_BINARY_DIR}/app/application)
target_link_libraries(omnigres PRIVATE ${libswipl})
target_compile_definitions(omnigres PRIVATE __SWI_PROLOG __SWI_EMBEDDED_)
target_include_directories(omnigres PRIVATE "${PLBASE}/include")
target_link_directories(omnigres PRIVATE "${PLLIBDIR}")

set_target_properties(omnigres PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
if(APPLE)
    set_target_properties(omnigres PROPERTIES INSTALL_RPATH "@loader_path")
else()
    set_target_properties(omnigres PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

if(STATIC)
    if(APPLE)
        find_library(CoreFoundation NAMES CoreFoundation)
        target_link_libraries(omnigres PRIVATE ${CoreFoundation})
        set_target_properties(omnigres PROPERTIES ENABLE_EXPORTS ON)
    endif()

    find_library(libncurses NAMES ncurses REQUIRED)
    find_library(libz NAMES z REQUIRED)
    find_library(libm NAMES m REQUIRED)

    target_link_libraries(omnigres PRIVATE ${libncurses} ${libz} ${libm})
endif()

set(omnigres_SOURCES cli.lgt env.lgt errors.pl loader.lgt omnigres.lgt pgwire.lgt pkgconfig.lgt postgres.lgt)

add_custom_command(
        DEPENDS ${omnigres_SOURCES} settings-build.lgt
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/app/application
        COMMAND LOGTALKUSER=${Logtalk_SOURCE_DIR} LOGTALKHOME=${Logtalk_SOURCE_DIR} PATH=${Logtalk_SOURCE_DIR}/integration:$ENV{PATH}
        ${Logtalk_SOURCE_DIR}/scripts/embedding/swipl/swipl_logtalk_qlf.sh
        -f save
        -d ${CMAKE_CURRENT_BINARY_DIR}/app
        -s ${CMAKE_CURRENT_SOURCE_DIR}/settings-build.lgt
        -l ${CMAKE_CURRENT_SOURCE_DIR}/loader.lgt
        -c -x
        # Trigger a rebuild for omnigres. Not pretty but does the job
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_SOURCE_DIR}/main.c)


add_custom_command(TARGET omnigres POST_BUILD
        COMMAND cat $<TARGET_FILE:omnigres> ${CMAKE_CURRENT_BINARY_DIR}/app/application > ${CMAKE_CURRENT_BINARY_DIR}/omnigres1
        COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/omnigres1 $<TARGET_FILE:omnigres>
        COMMAND chmod +x $<TARGET_FILE:omnigres>)

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
    add_dependencies(omnigres copy-${LIBSWIPL_})
endif()

enable_testing()
add_test(NAME omnigres_test COMMAND ${Logtalk_SOURCE_DIR}/scripts/logtalk_tester.sh -p swi -l 1
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})