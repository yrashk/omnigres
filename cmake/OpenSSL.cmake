# MIT License
#
# Copyright (c) 2015-2023 The ViaDuck Project
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

# build openssl locally

if(NOT DEFINED OPENSSL_CONFIGURED)
    CPMAddPackage(NAME openssl GIT_REPOSITORY https://github.com/openssl/openssl GIT_TAG openssl-3.1.4 VERSION 3.1.4 DOWNLOAD_ONLY)
    if(NOT openssl_ADDED)
        message(FATAL_ERROR "OpenSSL can't be added")
    endif()

    # includes
    include(ProcessorCount)
    include(ExternalProject)
    include(CPM)

    # find packages
    find_package(Git REQUIRED)
    find_package(PythonInterp 3 REQUIRED)

    # set variables
    ProcessorCount(NUM_JOBS)
    set(OS " UNIX ")

    # if already built, do not build again
    if((EXISTS ${OPENSSL_LIBSSL_PATH}) AND (EXISTS ${OPENSSL_LIBCRYPTO_PATH}))
        message(WARNING " Not building OpenSSL again. Remove ${OPENSSL_LIBSSL_PATH} and ${OPENSSL_LIBCRYPTO_PATH} for rebuild")
    else()
        find_program(MAKE_PROGRAM make)


        # additional configure script parameters
        set(CONFIGURE_OPENSSL_PARAMS --libdir=lib)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
            set(CONFIGURE_OPENSSL_PARAMS " ${CONFIGURE_OPENSSL_PARAMS} no-asm -g3 -O0 -fno-omit-frame-pointer -fno-inline-functions")
        endif()

        set(CONFIGURE_OPENSSL_MODULES ${CONFIGURE_OPENSSL_MODULES} no-tests)
        set(COMMAND_CONFIGURE ./config ${CONFIGURE_OPENSSL_PARAMS} ${CONFIGURE_OPENSSL_MODULES})

    endif()

    # add openssl target
    ExternalProject_Add(openssl
            URL https://mirror.viaduck.org/openssl/openssl-${OPENSSL_BUILD_VERSION}.tar.gz
            ${OPENSSL_CHECK_HASH}
            UPDATE_COMMAND " "

            CONFIGURE_COMMAND ${BUILD_ENV_TOOL} <SOURCE_DIR> ${COMMAND_CONFIGURE}
            PATCH_COMMAND ${PATCH_PROGRAM} -p1 --forward -r - < ${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-Fix-test_cms-if-DSA-is-not-supported.patch || echo

            BUILD_COMMAND ${BUILD_ENV_TOOL} <SOURCE_DIR> ${MAKE_PROGRAM} -j ${NUM_JOBS}
            BUILD_BYPRODUCTS ${OPENSSL_LIBSSL_PATH} ${OPENSSL_LIBCRYPTO_PATH}

            TEST_BEFORE_INSTALL 1
            TEST_COMMAND ${COMMAND_TEST}

            INSTALL_COMMAND ${BUILD_ENV_TOOL} <SOURCE_DIR> ${PERL_PATH_FIX_INSTALL}
            COMMAND ${BUILD_ENV_TOOL} <SOURCE_DIR> ${MAKE_PROGRAM} DESTDIR=${CMAKE_CURRENT_BINARY_DIR} install_sw ${INSTALL_OPENSSL_MAN}
            COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} ${CMAKE_BINARY_DIR}                    # force CMake-reload

            LOG_INSTALL 1
    )

    # set git config values to openssl requirements (no impact on linux though)
    ExternalProject_Add_Step(openssl setGitConfig
            COMMAND ${GIT_EXECUTABLE} config --global core.autocrlf false
            COMMAND ${GIT_EXECUTABLE} config --global core.eol lf
            DEPENDEES
            DEPENDERS download
            ALWAYS ON
    )

    # set, don't abort if it fails (due to variables being empty). To realize this we must only call git if the configs
    # are set globally, otherwise do a no-op command (" echo 1", since "true " is not available everywhere)
    if(GIT_CORE_AUTOCRLF)
        set(GIT_CORE_AUTOCRLF_CMD ${GIT_EXECUTABLE} config --global core.autocrlf ${GIT_CORE_AUTOCRLF})
    else()
        set(GIT_CORE_AUTOCRLF_CMD echo)
    endif()
    if(GIT_CORE_EOL)
        set(GIT_CORE_EOL_CMD ${GIT_EXECUTABLE} config --global core.eol ${GIT_CORE_EOL})
    else()
        set(GIT_CORE_EOL_CMD echo)
    endif()
    ##

    # set git config values to previous values
    ExternalProject_Add_Step(openssl restoreGitConfig
            # unset first (is required, since old value could be omitted, which wouldn't take any effect in " set "
            COMMAND ${GIT_EXECUTABLE} config --global --unset core.autocrlf
            COMMAND ${GIT_EXECUTABLE} config --global --unset core.eol

            COMMAND ${GIT_CORE_AUTOCRLF_CMD}
            COMMAND ${GIT_CORE_EOL_CMD}

            DEPENDEES download
            DEPENDERS configure
            ALWAYS ON
    )

    # write environment to file, is picked up by python script
    get_cmake_property(_variableNames VARIABLES)
    foreach(_variableName ${_variableNames})
        if(NOT _variableName MATCHES " lines")
            set(OUT_FILE "${OUT_FILE} ${_variableName}=\"${${_variableName}}\"\n ")
        endif()
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/buildenv.txt ${OUT_FILE})

    set_target_properties(ssl_lib PROPERTIES IMPORTED_LOCATION ${OPENSSL_LIBSSL_PATH})
    set_target_properties(crypto_lib PROPERTIES IMPORTED_LOCATION ${OPENSSL_LIBCRYPTO_PATH})

    set(OPENSSL_ROOT_DIR ${OPENSSL_PREIX} CACHE INTERNAL " OpenSSL")
    set(OPENSSL_USE_STATIC_LIBS ON)
    set(OPENSSL_CONFIGURED TRUE CACHE INTERNAL "OpenSSL")

endif()

#if(NOT DEFINED OPENSSL_CONFIGURED)
#    if(APPLE)
#        execute_process(COMMAND brew --prefix openssl@3.1
#                OUTPUT_VARIABLE OPENSSL_PREIX RESULT_VARIABLE OPENSSL_RC
#                OUTPUT_STRIP_TRAILING_WHITESPACE)

#        if(NOT OPENSSL_RC EQUAL 0)
#            message(FATAL_ERROR " No OpenSSL found, use homebrew to install one")
#        endif()

#        message(STATUS "Found OpenSSL at ${OPENSSL_PREIX} ")
#        set(OPENSSL_ROOT_DIR ${OPENSSL_PREIX} CACHE INTERNAL " OpenSSL")
#    elseif(UNIX)
#        find_package(PkgConfig)
#        pkg_check_modules(_OPENSSL openssl)
#    endif()

#    set(OPENSSL_USE_STATIC_LIBS ON)
#    set(OPENSSL_CONFIGURED TRUE CACHE INTERNAL "OpenSSL ")

#endif()