# Copyright 2014-2015, Max Planck Society.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.

# File created by Raffi Enficiaud

set(thirdparty_dir ${CMAKE_SOURCE_DIR}/thirdparty)

# the location where the archives will be deflated
set(thirdparties_deflate_directory ${CMAKE_BINARY_DIR}/external_libs_deflate)
if(NOT EXISTS ${thirdparties_deflate_directory})
  file(MAKE_DIRECTORY ${thirdparties_deflate_directory})
endif()

# custom cmake packages, should have lower priority than the ones bundled with cmake
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake_modules/ )

# these variables allow to specify to which the main project will link and
# to potentially copy some resources to the output directory of the main project.
# They are used by the CMakeLists.txt calling this file.

set(PHD_LINK_EXTERNAL)          # target to which the phd2 main library will link to
set(PHD_COPY_EXTERNAL_ALL)      # copy of a file for any configuration
set(PHD_COPY_EXTERNAL_DBG)      # copy for debug only
set(PHD_COPY_EXTERNAL_REL)      # copy for release only
set(PHD_EXTERNAL_PROJECT_DEPENDENCIES)

if(WIN32)
  # Reject 32-bit toolchains as early as possible. CMAKE_GENERATOR_PLATFORM
  # catches the Visual Studio "-A Win32" case, but is empty for Ninja /
  # Unix Makefiles; CMAKE_SIZEOF_VOID_P (set by project() before this file
  # is included) catches the actual target bitness regardless of generator.
  if(DEFINED CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
      "Unsupported target architecture: ${CMAKE_SIZEOF_VOID_P}-byte pointers. "
      "This fork builds x64 only.")
  endif()
  if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR
      "Unsupported generator platform '${CMAKE_GENERATOR_PLATFORM}'. "
      "This fork builds x64 only; configure with -A x64.")
  endif()
  set(WINDOWS_ARCH "x64")
endif()

if(APPLE)
  # make sure not to pick up any homebrew or macports dependencies
  set(CMAKE_IGNORE_PREFIX_PATH /opt/local)
endif()

# this module will be used to find system installed libraries on Linux
if(UNIX AND NOT APPLE)
  find_package(PkgConfig)
endif()

if(WIN32)
  include(FetchContent)
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_Declare(
    vcpkg
    GIT_REPOSITORY https://github.com/microsoft/vcpkg.git
    # vcpkg release tag: 2026.03.18
    GIT_TAG c3867e714dd3a51c272826eea77267876517ed99
    # Invoke the bootstrap script and vcpkg by absolute path via the
    # <SOURCE_DIR> token. A bare "bootstrap-vcpkg.bat" / "vcpkg" is not
    # resolved by CMake's command launcher (it does not search the step's
    # working directory the way an interactive shell does), which fails the
    # populate step with "'bootstrap-vcpkg.bat' is not recognized".
    UPDATE_COMMAND <SOURCE_DIR>/bootstrap-vcpkg.bat -disableMetrics
    COMMAND ${CMAKE_COMMAND} -E echo "Building vcpkg cfitsio"
    COMMAND <SOURCE_DIR>/vcpkg install --binarysource=default --no-print-usage cfitsio:${WINDOWS_ARCH}-windows
    COMMAND ${CMAKE_COMMAND} -E echo "Building vcpkg curl[ssl]"
    COMMAND <SOURCE_DIR>/vcpkg install --binarysource=default --no-print-usage curl[ssl]:${WINDOWS_ARCH}-windows
    COMMAND ${CMAKE_COMMAND} -E echo "Building vcpkg eigen3"
    COMMAND <SOURCE_DIR>/vcpkg install --binarysource=default --no-print-usage eigen3:${WINDOWS_ARCH}-windows
    COMMAND ${CMAKE_COMMAND} -E echo "Building vcpkg opencv4"
    COMMAND <SOURCE_DIR>/vcpkg install --binarysource=default --no-print-usage opencv4:${WINDOWS_ARCH}-windows
  )
  message(STATUS "Preparing VCPKG")
  FetchContent_MakeAvailable(vcpkg)
  set(VCPKG_PREFIX ${vcpkg_SOURCE_DIR}/installed/${WINDOWS_ARCH}-windows)
  set(VCPKG_DEBUG_BIN ${VCPKG_PREFIX}/debug/bin)
  set(VCPKG_RELEASE_BIN ${VCPKG_PREFIX}/bin)
  set(VCPKG_DEBUG_LIB ${VCPKG_PREFIX}/debug/lib)
  set(VCPKG_RELEASE_LIB ${VCPKG_PREFIX}/lib)
  set(VCPKG_INCLUDE ${VCPKG_PREFIX}/include)
  include_directories(${VCPKG_INCLUDE})
endif()

if(APPLE)
  find_library(iokitFramework          IOKit)
  find_library(carbonFramework         Carbon)
  find_library(cocoaFramework          Cocoa)
  find_library(systemFramework         System)
  find_library(webkitFramework         Webkit)
  find_library(audioToolboxFramework   AudioToolbox)
  find_library(openGLFramework         OpenGL)
  find_library(coreFoundationFramework CoreFoundation)
endif()

#############################################
#
# external rules common to all platforms
#
#############################################

##############################################
# cfitsio

if(WIN32)
  include_directories(${VCPKG_INCLUDE}/cfitsio)
  list(APPEND PHD_LINK_EXTERNAL_DEBUG
      ${VCPKG_DEBUG_LIB}/cfitsio.lib
      ${VCPKG_DEBUG_LIB}/zlibd.lib
  )
  list(APPEND PHD_LINK_EXTERNAL_RELEASE
      ${VCPKG_RELEASE_LIB}/cfitsio.lib
      ${VCPKG_RELEASE_LIB}/zlib.lib
  )
  list(APPEND PHD_COPY_EXTERNAL_DBG
      ${VCPKG_DEBUG_BIN}/cfitsio.dll
      ${VCPKG_DEBUG_BIN}/zlibd1.dll
  )
  list(APPEND PHD_COPY_EXTERNAL_REL
      ${VCPKG_RELEASE_BIN}/cfitsio.dll
      ${VCPKG_RELEASE_BIN}/zlib1.dll
  )
else()
  # On macOS the Homebrew bottle ships only the dylib; we bundle it into
  # "OpenAstro PHD2.app"/Contents/Frameworks/ via a POST_BUILD step in CMakeLists.txt
  # and rewrite install names so the .app stays redistributable.
  find_package(CFITSIO REQUIRED)
  include_directories(${CFITSIO_INCLUDE_DIR})
  list(APPEND PHD_LINK_EXTERNAL ${CFITSIO_LIBRARIES})
  message(STATUS "Using system's CFITSIO.")
endif()

#############################################
# libcurl
#############################################

if(WIN32)
  list(APPEND PHD_LINK_EXTERNAL_DEBUG
      ${VCPKG_DEBUG_LIB}/libcurl-d.lib
  )
  list(APPEND PHD_LINK_EXTERNAL_RELEASE
      ${VCPKG_RELEASE_LIB}/libcurl.lib
  )
  list(APPEND PHD_COPY_EXTERNAL_DBG
      ${VCPKG_DEBUG_BIN}/libcurl-d.dll
  )
  list(APPEND PHD_COPY_EXTERNAL_REL
      ${VCPKG_RELEASE_BIN}/libcurl.dll
  )
else()
  if(APPLE)
    find_library(CURL_LIBRARIES
                 NAMES curl
                 PATHS /usr/lib
    )
    if(NOT CURL_LIBRARIES)
      message(FATAL_ERROR "libcurl not found")
    endif()
    set(CURL_FOUND True)
    set(CURL_INCLUDE_DIRS /usr/include)
  else()
    find_package(CURL REQUIRED)
  endif()
  message(STATUS "using libcurl ${CURL_LIBRARIES}")
  include_directories(${CURL_INCLUDE_DIRS})
  list(APPEND PHD_LINK_EXTERNAL ${CURL_LIBRARIES})
endif()

#############################################
# the Eigen library, mostly header only

if(WIN32)
  set(EIGEN_SRC ${VCPKG_INCLUDE}/eigen3)
else()
  find_package(Eigen3 REQUIRED)
  # Eigen 5.x exposes the include path via the Eigen3::Eigen target only
  # (EIGEN3_INCLUDE_DIR was dropped). Older configs still set the variable;
  # fall back to it so this works on Debian/Pi too.
  if(TARGET Eigen3::Eigen)
    get_target_property(EIGEN_SRC Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
  else()
    set(EIGEN_SRC ${EIGEN3_INCLUDE_DIR})
  endif()
  message(STATUS "Using system's Eigen3 (${EIGEN_SRC}).")
endif()

#############################################
# Google test
# https://github.com/google/googletest/tree/main/googletest#incorporating-into-an-existing-cmake-project

if(USE_SYSTEM_GTEST)
  find_package(GTest REQUIRED)
  message(STATUS "Using system's gtest")
else()
  include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
      # Integrity pin for the exact v1.17.0 archive above. Without URL_HASH,
      # FetchContent accepts whatever bytes the URL returns, so a compromised
      # or corrupted download would be built silently. Verify with:
      #   curl -L https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz | sha256sum
      URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  )
  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

#############################################
# wxWidgets
#
# The usage is a bit different on all the platforms. For having
#  version >= 3.0, a version of cmake >= 3.0 should be used on Windows
#  on Linux/macOS it works properly this way).

set(wxWidgets_PREFIX_DIRECTORY $ENV{WXWIN} CACHE PATH "wxWidgets directory")

if(WIN32)
  # wxWidgets
  set(wxWidgets_CONFIGURATION msw CACHE STRING "Set wxWidgets configuration")

  if(NOT wxWidgets_PREFIX_DIRECTORY OR NOT EXISTS ${wxWidgets_PREFIX_DIRECTORY})
    message(FATAL_ERROR "The variable wxWidgets_PREFIX_DIRECTORY should be defined and should point to a valid wxWindows installation path. See the open-phd-guiding wiki for more information.")
  endif()

  set(wxWidgets_ROOT_DIR ${wxWidgets_PREFIX_DIRECTORY})
  set(wxWidgets_LIB_DIR ${wxWidgets_ROOT_DIR}/lib/vc_x64_lib)
  set(wxWidgets_USE_STATIC ON)
  set(wxWidgets_USE_DEBUG ON)
  set(wxWidgets_USE_UNICODE OFF)
  find_package(wxWidgets 3.2 REQUIRED COMPONENTS propgrid base core aui adv html net)
  include(${wxWidgets_USE_FILE})

elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  if(NOT DEFINED wxWidgets_PREFIX_DIRECTORY)
    set(wxWidgets_PREFIX_DIRECTORY "/usr/local")
  endif()
  set(wxWidgets_CONFIG_OPTIONS --prefix=${wxWidgets_PREFIX_DIRECTORY})

  find_program(wxWidgets_CONFIG_EXECUTABLE
    NAMES "wxgtk3u-3.1-config"
    PATHS ${wxWidgets_PREFIX_DIRECTORY}/bin NO_DEFAULT_PATH)
  if(NOT wxWidgets_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "Cannot find wxWidgets_CONFIG_EXECUTABLE from the given directory ${wxWidgets_PREFIX_DIRECTORY}")
  endif()

  set(wxRequiredLibs aui core base adv html net)
  execute_process(COMMAND ${wxWidgets_CONFIG_EXECUTABLE} --libs ${wxRequiredLibs}
                  OUTPUT_VARIABLE wxWidgets_LIBRARIES
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  separate_arguments(${wxWidgets_LIBRARIES})
  execute_process(COMMAND ${wxWidgets_CONFIG_EXECUTABLE} --cflags ${wxRequiredLibs}
                  OUTPUT_VARIABLE wxWidgets_CXXFLAGS
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  separate_arguments(wxWidgets_CXX_FLAGS UNIX_COMMAND "${wxWidgets_CXXFLAGS}")
  separate_arguments(wxWidgets_LDFLAGS UNIX_COMMAND "${wxWidgets_LDFLAGS}")
else()
  if(wxWidgets_PREFIX_DIRECTORY)
    set(wxWidgets_CONFIG_OPTIONS --prefix=${wxWidgets_PREFIX_DIRECTORY})

    find_program(wxWidgets_CONFIG_EXECUTABLE NAMES "wx-config" PATHS ${wxWidgets_PREFIX_DIRECTORY}/bin NO_DEFAULT_PATH)
    if(NOT wxWidgets_CONFIG_EXECUTABLE)
      message(FATAL_ERROR "Cannot find wxWidgets_CONFIG_EXECUTABLE from the given directory ${wxWidgets_PREFIX_DIRECTORY}")
    endif()
  endif()

  find_package(wxWidgets 3.2 REQUIRED COMPONENTS aui core base adv html net)
  if(NOT wxWidgets_FOUND)
    message(FATAL_ERROR "wxWidgets >= 3.2 cannot be found. Please use wx-config prefix")
  endif()
endif()

list(APPEND PHD_LINK_EXTERNAL ${wxWidgets_LIBRARIES})

#############################################
#
#  INDI (all platforms; Windows builds libindi 2.x via vcpkg)
#
#############################################

set(INDI_MIN_VERSION "2.0.0")

if(USE_SYSTEM_LIBINDI)
  message(STATUS "Using system's libindi")
  find_package(INDI ${INDI_MIN_VERSION} REQUIRED)
  list(APPEND PHD_LINK_EXTERNAL ${INDI_CLIENT_LIBRARIES})

  find_package(ZLIB REQUIRED)
  list(APPEND PHD_LINK_EXTERNAL ${ZLIB_LIBRARIES})

  find_package(Nova REQUIRED)
  add_definitions("-DLIBNOVA")
  include_directories(${NOVA_INCLUDE_DIR})
  list(APPEND PHD_LINK_EXTERNAL ${NOVA_LIBRARIES})
else()
  include(ExternalProject)
  set(indi_INSTALL_DIR ${CMAKE_BINARY_DIR}/libindi)
  ExternalProject_Add(
    indi
    GIT_REPOSITORY https://github.com/indilib/indi.git
    GIT_TAG 6c99d6c033dbf23f3c8d5772f20720d355755fb1  # v2.2.1.1
    CMAKE_ARGS -Wno-dev
      -DINDI_BUILD_SERVER=OFF
      -DINDI_BUILD_DRIVERS=OFF
      -DINDI_BUILD_COMMON=OFF
      -DINDI_BUILD_CLIENT=ON
      -DINDI_BUILD_QT5_CLIENT=OFF
      -DINDI_BUILD_SHARED=OFF
      -DCMAKE_PREFIX_PATH=${VCPKG_PREFIX}
      -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/libindi
      -DCMAKE_CXX_FLAGS=-D_CRT_SECURE_NO_WARNINGS
      -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
  )
  # SYSTEM suppresses warnings from INDI headers (e.g. -Wnontrivial-memcall
  # on memset(this, 0, sizeof(*this)) in indipropertyview.h). Not our code,
  # not our bug to fix; we just don't want it cluttering our build output.
  include_directories(SYSTEM ${indi_INSTALL_DIR}/include)
  if(WIN32)
    list(APPEND PHD_LINK_EXTERNAL ${indi_INSTALL_DIR}/lib/indiclient.lib)
  else()
    list(APPEND PHD_LINK_EXTERNAL ${indi_INSTALL_DIR}/lib/libindiclient.a)
    if(APPLE)
      # MacOS must use a static libnova to avoid introducing a homebrew or
      # macports dylib dependency. CMAKE_PREFIX_PATH (set in run_cmake-macos)
      # points find_library at /opt/homebrew on Apple Silicon and /usr/local
      # on Intel, so no hardcoded path is needed here.
      find_library(LIBNOVA REQUIRED NAMES libnova.a)
      # zlib is needed for INDI's basedevice (uncompress); macOS ships it as
      # a system dylib at /usr/lib/libz.dylib, no install required.
      list(APPEND PHD_LINK_EXTERNAL ${LIBNOVA} z)
    else()
      find_library(LIBNOVA REQUIRED NAMES nova)
      list(APPEND PHD_LINK_EXTERNAL ${LIBNOVA} z)
    endif()
    ## Define LIBNOVA when building Indi from source.
    add_definitions("-DLIBNOVA")
  endif()
  # phd2 must depend on the INDI ExternalProject — otherwise make has no
  # rule to produce libindiclient.a before the phd2 target tries to link.
  list(APPEND PHD_EXTERNAL_PROJECT_DEPENDENCIES indi)
endif()

#############################################
#
# Windows specific dependencies
# - Visual Leak Detector (optional)
# - OpenCV
# - Video For Windows (vfw)
# - ASCOM camera stuff
#############################################

if(WIN32)

  if(NOT DISABLE_VLD)
    find_path(VLD_INCLUDE vld.h
        HINTS "C:/Program Files (x86)/Visual Leak Detector" ENV VLD_DIR
        PATH_SUFFIXES include
    )
    if (VLD_INCLUDE)
      get_filename_component(VLD_ROOT ${VLD_INCLUDE} DIRECTORY)
      add_definitions(-DHAVE_VLD=1)
      message(STATUS "Enabling VLD (${VLD_ROOT})")
    else()
      message(STATUS "Disabling VLD: VLD not found")
    endif()
  else()
    message(STATUS "Disabling VLD: DISABLE_VLD is set")
  endif()

  include_directories(${VCPKG_INCLUDE}/opencv2)
  list(APPEND PHD_LINK_EXTERNAL_DEBUG
      ${VCPKG_DEBUG_LIB}/opencv_imgproc4d.lib
      ${VCPKG_DEBUG_LIB}/opencv_highgui4d.lib
      ${VCPKG_DEBUG_LIB}/opencv_core4d.lib
      ${VCPKG_DEBUG_LIB}/opencv_videoio4d.lib
      ${VCPKG_DEBUG_LIB}/opencv_imgcodecs4d.lib
  )
  list(APPEND PHD_LINK_EXTERNAL_RELEASE
      ${VCPKG_RELEASE_LIB}/opencv_imgproc4.lib
      ${VCPKG_RELEASE_LIB}/opencv_highgui4.lib
      ${VCPKG_RELEASE_LIB}/opencv_core4.lib
      ${VCPKG_RELEASE_LIB}/opencv_videoio4.lib
      ${VCPKG_RELEASE_LIB}/opencv_imgcodecs4.lib
  )
  list(APPEND PHD_COPY_EXTERNAL_DBG
      ${VCPKG_DEBUG_BIN}/opencv_imgproc4d.dll
      ${VCPKG_DEBUG_BIN}/opencv_highgui4d.dll
      ${VCPKG_DEBUG_BIN}/opencv_core4d.dll
      ${VCPKG_DEBUG_BIN}/opencv_videoio4d.dll
      ${VCPKG_DEBUG_BIN}/opencv_imgcodecs4d.dll
      ${VCPKG_DEBUG_BIN}/jpeg62.dll
      ${VCPKG_DEBUG_BIN}/libpng16d.dll
      ${VCPKG_DEBUG_BIN}/tiffd.dll
      ${VCPKG_DEBUG_BIN}/liblzma.dll
      ${VCPKG_DEBUG_BIN}/libwebp.dll
      ${VCPKG_DEBUG_BIN}/libwebpdecoder.dll
      ${VCPKG_DEBUG_BIN}/libsharpyuv.dll
  )
  list(APPEND PHD_COPY_EXTERNAL_REL
      ${VCPKG_RELEASE_BIN}/opencv_imgproc4.dll
      ${VCPKG_RELEASE_BIN}/opencv_highgui4.dll
      ${VCPKG_RELEASE_BIN}/opencv_core4.dll
      ${VCPKG_RELEASE_BIN}/opencv_videoio4.dll
      ${VCPKG_RELEASE_BIN}/opencv_imgcodecs4.dll
      ${VCPKG_RELEASE_BIN}/jpeg62.dll
      ${VCPKG_RELEASE_BIN}/libpng16.dll
      ${VCPKG_RELEASE_BIN}/tiff.dll
      ${VCPKG_RELEASE_BIN}/liblzma.dll
      ${VCPKG_RELEASE_BIN}/libwebp.dll
      ${VCPKG_RELEASE_BIN}/libwebpdecoder.dll
      ${VCPKG_RELEASE_BIN}/libsharpyuv.dll
  )
endif()

# Camera SDK libraries removed - Alpaca only build

if(WIN32)
  # Windows runtime dependencies
  list(APPEND PHD_LINK_EXTERNAL
    winmm.lib
    ws2_32.lib
  )

  list(APPEND PHD_COPY_EXTERNAL_ALL
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/msvcp140.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/msvcp140_1.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/msvcp140_2.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/msvcp140_atomic_wait.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/msvcp140_codecvt_ids.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/vcomp140.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/vcruntime140.dll
    # vcruntime140_1.dll is REQUIRED by any x64 binary built with VS2019+;
    # omitting it made phd2.exe fail to launch on clean Windows 10 systems
    # that never had the VC++ redistributable installed.
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/vcruntime140_1.dll
    ${PHD_PROJECT_ROOT_DIR}/WinLibs/x64/concrt140.dll
  )

endif()

#############################################
#
# macOS specific dependencies
#
#############################################
if(APPLE)
  list(APPEND PHD_LINK_EXTERNAL
    ${IOKit}
    ${Carbon}
    ${Cocoa}
    ${System}
    ${Webkit}
    ${AudioToolbox}
    ${OpenGL}
  )

  find_path(CARBON_INCLUDE_DIR Carbon.h)

endif()  # APPLE

#############################################
#
# Unix/Linux specific dependencies
#
#############################################
if(UNIX AND NOT APPLE)

  # math library is needed, and should be one of the last things to link to here
  find_library(mathlib NAMES m)
  list(APPEND PHD_LINK_EXTERNAL ${mathlib})

endif()

#############################################
#
# gettext and msgmerge tools for documentation/internationalization
#
#############################################

# zip file support integrated in cmake 3.2+
if(WIN32 AND ("${CMAKE_VERSION}" VERSION_GREATER "3.2")
         AND ("${GETTEXT_ROOT}" STREQUAL ""))

  # GETTEXT_ROOT not given from the command line: deflating our own

  set(GETTEXTTOOLS gettext-0.14.4)
  set(GETTEXT_ROOT ${thirdparties_deflate_directory}/${GETTEXTTOOLS})

  # deflate
  if(NOT EXISTS ${GETTEXT_ROOT})

    message(STATUS "Deflating gettexttools from thirdparties to ${GETTEXT_ROOT}")
    # create directory
    if(NOT EXISTS ${GETTEXT_ROOT})
      file(MAKE_DIRECTORY ${GETTEXT_ROOT})
    endif()

    # untar the dependency
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${thirdparty_dir}/${GETTEXTTOOLS}-bin.zip
      WORKING_DIRECTORY ${GETTEXT_ROOT})
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${thirdparty_dir}/${GETTEXTTOOLS}-dep.zip
      WORKING_DIRECTORY ${GETTEXT_ROOT})
  endif()

endif()

set(GETTEXT_FINDPROGRAM_OPTIONS)
if(NOT ("${GETTEXT_ROOT}" STREQUAL ""))
  set(GETTEXT_FINDPROGRAM_OPTIONS
      PATHS ${GETTEXT_ROOT}
               PATH_SUFFIXES bin
               DOC "gettext program deflated from the thirdparties"
               NO_DEFAULT_PATH)
endif()

find_program(XGETTEXT
             NAMES xgettext
             ${GETTEXT_FINDPROGRAM_OPTIONS})

find_program(MSGFMT
              NAMES msgfmt
             ${GETTEXT_FINDPROGRAM_OPTIONS})

find_program(MSGMERGE
              NAMES msgmerge
             ${GETTEXT_FINDPROGRAM_OPTIONS})

if(NOT XGETTEXT)
  message(STATUS "'xgettext' program not found")
else()
  message(STATUS "'xgettext' program found at '${XGETTEXT}'")
endif()

if(NOT MSGFMT)
  message(STATUS "'msgfmt' program not found")
else()
  message(STATUS "'msgfmt' program found at '${MSGFMT}'")
endif()

if(NOT MSGMERGE)
  message(STATUS "'msgmerge' program not found")
else()
  message(STATUS "'msgmerge' program found at '${MSGMERGE}'")
endif()
