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

if(USE_SYSTEM_GTEST)
  cmake_minimum_required(VERSION 3.16)
else()
  # If the system gtest is not available (especially Windows and Mac
  # builds) gtest is incorporated using FetchContent which requires
  # cmake 3.24
  cmake_minimum_required(VERSION 3.24)
endif()

# this must come before any enable_language() or project()
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS deployment version")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
endif()

if(WINDOWS)
 set(CMAKE_GENERATOR "Visual Studio 17" CACHE STRING INTERNAL "" FORCE)
endif()

enable_language(C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
    if(POLICY CMP0043)
      cmake_policy(SET CMP0043 NEW)
    endif()
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# root directory of the project
set(PHD_PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})
set(phd_src_dir ${PHD_PROJECT_ROOT_DIR}/src)

# Additional files in the workspace, To improve maintainability
add_custom_target(CmakeAdditionalFiles
  SOURCES
  ${PHD_PROJECT_ROOT_DIR}/thirdparty/thirdparty.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/FindASCOM_INTERFACE.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/FindCFITSIO.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/FindINDI.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/FindNova.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/PHD2BuildDoc.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/PHD2GenerateDocScript.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/PHD2Removegettextwarning.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/compiler_options.cmake
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/PHD2Packaging.cmake)

# compiler options
include(cmake_modules/compiler_options.cmake)

# declare useful functions about documentation
include(cmake_modules/PHD2BuildDoc.cmake)

# retrieves the version from version.md
get_phd_version()
set(PHD2_VERSION_CORE "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(PHD2_VERSION_SUFFIX "${VERSION_SUFFIX}")
set(PHD2_VERSION_FULL "${PHD2_VERSION_CORE}${PHD2_VERSION_SUFFIX}")
message(STATUS "PHD2 version '${PHD2_VERSION_FULL}' (from version.md)")

# Generate compile-time version macros from version.md
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
  ${PHD_PROJECT_ROOT_DIR}/cmake_modules/phd_version.h.in
  ${CMAKE_BINARY_DIR}/generated/phd_version.h
  @ONLY
)







#################################################################################
#
# general defines
if(UNIX AND NOT APPLE)
  if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr CACHE PATH "PHD2 install path" FORCE)
  endif()
endif()

set(USE_FOLDERS TRUE)
set(BUILD_SHARED_LIBS FALSE)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)



#################################################################################
#
# main project
# this should appear after setting the architecture
project(phd2)
# include(CTest) defines the standard BUILD_TESTING option (default ON) and
# calls enable_testing(), so -DBUILD_TESTING=OFF is honored alongside
# -DPHD_BUILD_TESTS=OFF below.
include(CTest)


#################################################################################
#
# external libraries
# to be changed in add_subdirectory
include(thirdparty/thirdparty.cmake)


#################################################################################
#
# generates the documentation targets. Part of those targets define the
# zip files PHD2GuideHelp.zip. In order to include them in the main build
# we need to declare them here. Those targets should not depend on any source file
# but may require thirdparty (gettext etc).
generate_doc_targets(
  DEFAULT_LOCALE default_locale
  GENERATED_LOCALES all_locales)



#################################################################################
#
# contributions

# PEC Guider, Max Planck Institute for Intelligent Systems, Tuebingen, Germany.
add_subdirectory(contributions/MPI_IS_gaussian_process tmp_gaussian_process)

# Unit tests (uses GTest fetched in thirdparty.cmake). Added BEFORE the
# global include_directories(${phd_src_dir}) below so test targets don't
# inherit src/ on their include path; tests/include/phd.h must shadow the
# real one for header substitution to work.
#
# PHD_BUILD_TESTS defaults ON so packaging scripts (build-deb.sh,
# build-dmg.sh, build-exe.ps1) pick the suite up automatically. Pass
# -DPHD_BUILD_TESTS=OFF to skip building the test binaries.
option(PHD_BUILD_TESTS "Build PHD2 unit tests" ON)
if(PHD_BUILD_TESTS AND BUILD_TESTING)
  add_subdirectory(tests)
endif()



#################################################################################
#
# Global include directories
include_directories(${phd_src_dir})
include_directories(${CMAKE_BINARY_DIR}/generated)
include_directories(${PHD_PROJECT_ROOT_DIR})



#################################################################################
#
# main project

if(KWIQGuider_PLATFORM_SRC)
  source_group(Cameras\\cam_KWIQGuider FILES ${KWIQGuider_PLATFORM_SRC})
endif()

set(cam_SRC
  ${phd_src_dir}/cam_alpaca.cpp
  ${phd_src_dir}/cam_alpaca.h
  ${phd_src_dir}/alpaca_client.cpp
  ${phd_src_dir}/alpaca_client.h
  ${phd_src_dir}/alpaca_discovery.cpp
  ${phd_src_dir}/alpaca_discovery.h
  ${phd_src_dir}/config_alpaca.cpp
  ${phd_src_dir}/config_alpaca.h
  ${phd_src_dir}/scope_alpaca.h
  ${phd_src_dir}/scope_alpaca.cpp

  ${phd_src_dir}/camera.cpp
  ${phd_src_dir}/camera.h
  ${phd_src_dir}/cameras.h
)

# INDI camera support (all platforms; libindi client builds via vcpkg on Windows)
list(APPEND cam_SRC
  ${phd_src_dir}/cam_indi.cpp
  ${phd_src_dir}/cam_indi.h
)

# ASCOM camera support (Windows-only; gated by ASCOM_CAMERA macro in cameras.h).
# Files are always listed so the source tree is platform-independent; the macro
# compiles them out on non-Windows, mirroring how scope_ascom is handled above.
list(APPEND cam_SRC
  ${phd_src_dir}/cam_ascom.cpp
  ${phd_src_dir}/cam_ascom.h
)

source_group(Cameras FILES ${cam_SRC})

set(scopes_SRC
  ${phd_src_dir}/mount.cpp
  ${phd_src_dir}/mount.h
  ${phd_src_dir}/scope.cpp
  ${phd_src_dir}/scope.h
  ${phd_src_dir}/scope_ascom.cpp
  ${phd_src_dir}/scope_ascom.h
  ${phd_src_dir}/scope_manual_pointing.cpp
  ${phd_src_dir}/scope_manual_pointing.h
  ${phd_src_dir}/scopes.h
  ${phd_src_dir}/stepguider.cpp
  ${phd_src_dir}/stepguider.h
  ${phd_src_dir}/stepguiders.h
)

# INDI mount support (all platforms)
list(APPEND scopes_SRC
  ${phd_src_dir}/scope_indi.cpp
  ${phd_src_dir}/scope_indi.h
)
source_group(Scopes FILES ${scopes_SRC})

set(guiding_SRC
  ${phd_src_dir}/backlash_comp.cpp
  ${phd_src_dir}/backlash_comp.h
  ${phd_src_dir}/guide_algorithm_hysteresis.cpp
  ${phd_src_dir}/guide_algorithm_hysteresis.h
  ${phd_src_dir}/guide_algorithm_gaussian_process.cpp # MPI.IS PEC Guider: requires link to the GP target (contrib)
  ${phd_src_dir}/guide_algorithm_gaussian_process.h
  ${phd_src_dir}/guide_algorithm_identity.cpp
  ${phd_src_dir}/guide_algorithm_identity.h
  ${phd_src_dir}/guide_algorithm_lowpass.cpp
  ${phd_src_dir}/guide_algorithm_lowpass.h
  ${phd_src_dir}/guide_algorithm_lowpass2.cpp
  ${phd_src_dir}/guide_algorithm_lowpass2.h
  ${phd_src_dir}/guide_algorithm_resistswitch.cpp
  ${phd_src_dir}/guide_algorithm_resistswitch.h
  ${phd_src_dir}/guide_algorithm_zfilter.cpp
  ${phd_src_dir}/guide_algorithm_zfilter.h
  ${phd_src_dir}/guide_algorithm.cpp
  ${phd_src_dir}/guide_algorithm.h
  ${phd_src_dir}/guide_algorithms.h
  ${phd_src_dir}/guider_multistar.cpp
  ${phd_src_dir}/guider_multistar.h
  ${phd_src_dir}/guider.cpp
  ${phd_src_dir}/guider.h
  ${phd_src_dir}/guiders.h
  ${phd_src_dir}/zfilterfactory.cpp
  ${phd_src_dir}/zfilterfactory.h
)
source_group(Guiding FILES ${guiding_SRC})

set(phd2_SRC
  ${phd_src_dir}/about_dialog.cpp
  ${phd_src_dir}/about_dialog.h
  ${phd_src_dir}/advanced_dialog.cpp
  ${phd_src_dir}/advanced_dialog.h
  ${phd_src_dir}/aui_controls.cpp
  ${phd_src_dir}/aui_controls.h

  ${phd_src_dir}/calreview_dialog.cpp
  ${phd_src_dir}/calreview_dialog.h
  ${phd_src_dir}/calibration_assistant.cpp
  ${phd_src_dir}/calibration_assistant.h

  ${phd_src_dir}/calstep_dialog.cpp
  ${phd_src_dir}/calstep_dialog.h
  ${phd_src_dir}/camcal_import_dialog.cpp
  ${phd_src_dir}/camcal_import_dialog.h
  ${phd_src_dir}/circbuf.h

  ${phd_src_dir}/comet_tool.cpp
  ${phd_src_dir}/comet_tool.h

  ${phd_src_dir}/configdialog.cpp
  ${phd_src_dir}/configdialog.h
  ${phd_src_dir}/confirm_dialog.cpp
  ${phd_src_dir}/confirm_dialog.h
  ${phd_src_dir}/darks_dialog.cpp
  ${phd_src_dir}/darks_dialog.h
  ${phd_src_dir}/debuglog.cpp
  ${phd_src_dir}/debuglog.h
  ${phd_src_dir}/drift_tool.cpp
  ${phd_src_dir}/drift_tool.h
  ${phd_src_dir}/eegg.cpp
  ${phd_src_dir}/event_server.cpp
  ${phd_src_dir}/event_server.h

  ${phd_src_dir}/fitsiowrap.cpp
  ${phd_src_dir}/fitsiowrap.h

  ${phd_src_dir}/gear_dialog.cpp
  ${phd_src_dir}/gear_dialog.h
  ${phd_src_dir}/gear_simulator.h
  ${phd_src_dir}/gear_simulator.cpp
  ${phd_src_dir}/graph-stepguider.cpp
  ${phd_src_dir}/graph-stepguider.h
  ${phd_src_dir}/graph.cpp
  ${phd_src_dir}/graph.h
  ${phd_src_dir}/guiding_assistant.cpp
  ${phd_src_dir}/guiding_assistant.h
  ${phd_src_dir}/guidinglog.cpp
  ${phd_src_dir}/guidinglog.h
  ${phd_src_dir}/guiding_stats.cpp
  ${phd_src_dir}/guiding_stats.h
  ${phd_src_dir}/image_math.cpp
  ${phd_src_dir}/image_math.h
  ${phd_src_dir}/imagelogger.cpp
  ${phd_src_dir}/imagelogger.h
  ${phd_src_dir}/json_parser.cpp
  ${phd_src_dir}/json_parser.h
  ${phd_src_dir}/logger.cpp
  ${phd_src_dir}/logger.h
  ${phd_src_dir}/log_uploader.cpp
  ${phd_src_dir}/log_uploader.h
  ${phd_src_dir}/manualcal_dialog.cpp
  ${phd_src_dir}/manualcal_dialog.h
  ${phd_src_dir}/messagebox_proxy.cpp
  ${phd_src_dir}/messagebox_proxy.h
  ${phd_src_dir}/myframe.cpp
  ${phd_src_dir}/myframe.h
  ${phd_src_dir}/myframe_events.cpp
  ${phd_src_dir}/nudge_lock.cpp
  ${phd_src_dir}/nudge_lock.h
  ${phd_src_dir}/optionsbutton.cpp
  ${phd_src_dir}/optionsbutton.h
  ${phd_src_dir}/phd.cpp
  ${phd_src_dir}/phd.h
  ${phd_src_dir}/phdconfig.cpp
  ${phd_src_dir}/phdconfig.h
  ${phd_src_dir}/phdcontrol.cpp
  ${phd_src_dir}/phdcontrol.h
  ${phd_src_dir}/phdupdate.cpp
  ${phd_src_dir}/phdupdate.h
  ${phd_src_dir}/pierflip_tool.cpp
  ${phd_src_dir}/pierflip_tool.h
  ${phd_src_dir}/polardrift_tool.h
  ${phd_src_dir}/polardrift_toolwin.h
  ${phd_src_dir}/polardrift_toolwin.cpp
  ${phd_src_dir}/profile_wizard.h
  ${phd_src_dir}/profile_wizard.cpp
  ${phd_src_dir}/point.h
  ${phd_src_dir}/Refine_DefMap.cpp
  ${phd_src_dir}/Refine_DefMap.h

  # rotators
  ${phd_src_dir}/rotators.h

  ${phd_src_dir}/rotator.cpp
  ${phd_src_dir}/rotator.h

  ${phd_src_dir}/rotator_ascom.cpp
  ${phd_src_dir}/rotator_ascom.h

  ${phd_src_dir}/rotator_alpaca.cpp
  ${phd_src_dir}/rotator_alpaca.h

  ${phd_src_dir}/runinbg.cpp
  ${phd_src_dir}/runinbg.h


  ${phd_src_dir}/serialport.cpp
  ${phd_src_dir}/serialport.h
  ${phd_src_dir}/serialport_loopback.cpp
  ${phd_src_dir}/serialport_loopback.h
  ${phd_src_dir}/serialport_mac.cpp
  ${phd_src_dir}/serialport_mac.h
  ${phd_src_dir}/serialport_win32.cpp
  ${phd_src_dir}/serialport_win32.h
  ${phd_src_dir}/serialport_posix.cpp
  ${phd_src_dir}/serialport_posix.h
  ${phd_src_dir}/serialports.h
  ${phd_src_dir}/sha1.cpp
  ${phd_src_dir}/sha1.h
  ${phd_src_dir}/socket_server.cpp
  ${phd_src_dir}/socket_server.h
  ${phd_src_dir}/starcross_test.cpp
  ${phd_src_dir}/starcross_test.h
  ${phd_src_dir}/staticpa_tool.h
  ${phd_src_dir}/staticpa_toolwin.h
  ${phd_src_dir}/staticpa_toolwin.cpp
  ${phd_src_dir}/statswindow.cpp
  ${phd_src_dir}/statswindow.h

  ${phd_src_dir}/star.cpp
  ${phd_src_dir}/star.h
  ${phd_src_dir}/star_profile.cpp
  ${phd_src_dir}/star_profile.h
  ${phd_src_dir}/target.cpp
  ${phd_src_dir}/target.h
  ${phd_src_dir}/testguide.cpp
  ${phd_src_dir}/testguide.h
  ${phd_src_dir}/usImage.cpp
  ${phd_src_dir}/usImage.h
  ${phd_src_dir}/worker_thread.cpp
  ${phd_src_dir}/worker_thread.h
  ${phd_src_dir}/wxled.cpp
  ${phd_src_dir}/wxled.h
)

if (WIN32)
  list(APPEND phd2_SRC
      # windows-only sources
      ${phd_src_dir}/parallelports.h
      ${phd_src_dir}/parallelport.h
      ${phd_src_dir}/parallelport.cpp

      ${phd_src_dir}/parallelport_win32.h
      ${phd_src_dir}/parallelport_win32.cpp

      ${phd_src_dir}/comdispatch.h
      ${phd_src_dir}/comdispatch.cpp
  )
endif()

# INDI rotator + shared INDI config/GUI (all platforms)
list(APPEND phd2_SRC
    ${phd_src_dir}/rotator_indi.cpp
    ${phd_src_dir}/rotator_indi.h
    ${phd_src_dir}/config_indi.cpp
    ${phd_src_dir}/config_indi.h
    ${phd_src_dir}/indi_gui.cpp
    ${phd_src_dir}/indi_gui.h
    ${phd_src_dir}/indi_discovery.cpp
    ${phd_src_dir}/indi_discovery.h
)
source_group(src FILES ${phd2_SRC})

# Add the messages.mo directly in the project such that this triggers a new build
# and all the post_build commands are honored properly
foreach(current_locale IN LISTS all_locales)
  # installation of the document and translation files
  get_zip_file(current_zip LOCALE ${current_locale})
  get_translation_files(current_translations LOCALE ${current_locale})

  foreach(current_file IN LISTS current_translations)
    get_filename_component(message_file_basename ${current_file} NAME)
    get_filename_component(message_file_ext ${current_file} EXT)
    if(("${message_file_ext}" STREQUAL ".mo") OR ("${message_file_ext}" STREQUAL ".zip"))
      # message(STATUS "Harhar ${message_file_ext} / ${current_file}")
      list(APPEND phd2_SRC ${current_file})
    endif()
  endforeach()

endforeach() # for all locales

macro(ADD_MSVC_PRECOMPILED_HEADER PrecompiledHeader PrecompiledSource SourcesVar)
  if(MSVC)
    GET_FILENAME_COMPONENT(PrecompiledBasename ${PrecompiledHeader} NAME_WE)
    SET(PrecompiledBinary "\$(IntDir)/${PrecompiledBasename}.pch")
    SET(Sources ${${SourcesVar}})

    SET_SOURCE_FILES_PROPERTIES(${PrecompiledSource}
                                PROPERTIES COMPILE_FLAGS "/Yc\"${PrecompiledHeader}\" /Fp\"${PrecompiledBinary}\""
                                           OBJECT_OUTPUTS "${PrecompiledBinary}")
    SET_SOURCE_FILES_PROPERTIES(${Sources}
                                PROPERTIES COMPILE_FLAGS "/Yu\"${PrecompiledHeader}\" /FI\"${PrecompiledHeader}\" /Fp\"${PrecompiledBinary}\""
                                           OBJECT_DEPENDS "${PrecompiledBinary}")

    # Add precompiled header to SourcesVar
    LIST(APPEND ${SourcesVar} ${PrecompiledSource})
  endif()
endmacro(ADD_MSVC_PRECOMPILED_HEADER)

## #########################
## macOS
if(APPLE)

  # bundle definitions
  set(MACOSX_BUNDLE_BUNDLE_NAME          "OpenAstro PHD2" )
  set(MACOSX_BUNDLE_GUI_IDENTIFIER       "net.openastro.phd2" )
  set(MACOSX_BUNDLE_ICON_FILE            AppIcon.icns )
  set(MACOSX_BUNDLE_INFO_STRING          "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}, Copyright 2026 OpenAstro" )
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}" )
  set(MACOSX_BUNDLE_LONG_VERSION_STRING  "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" )
  set(MACOSX_BUNDLE_BUNDLE_VERSION       "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" )
  set(MACOSX_BUNDLE_COPYRIGHT            "Copyright 2026 OpenAstro. Based on PHD2 (C) 2010-2025 openphdguiding.org." )

  # main target
  add_executable(
    phd2
    MACOSX_BUNDLE
    ${scopes_SRC}
    ${cam_SRC}
    ${guiding_SRC}
    ${phd2_SRC}

    # resources of the bundle
    ${PHD_PROJECT_ROOT_DIR}/icons/${MACOSX_BUNDLE_ICON_FILE}

    # extra frameworks
    ${phd2_OSX_FRAMEWORKS}

    ${PHD_PROJECT_ROOT_DIR}/run_phd2_macos
  )
  set_target_properties(
    phd2
    PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${PHD_PROJECT_ROOT_DIR}/Info.plist.in
    # OUTPUT_NAME on a MACOSX_BUNDLE target sets BOTH the .app filename and the
    # binary inside Contents/MacOS/. We want users to see "OpenAstro PHD2" in
    # /Applications instead of "PHD2" (which collides on disk with upstream
    # PHD2.app if both are installed and looks wrong next to our other
    # OpenAstro-branded artifacts). The bundle binary becomes
    # `OpenAstro PHD2.app/Contents/MacOS/OpenAstro PHD2`; run_phd2_macos is
    # the actual CFBundleExecutable (see Info.plist.in) and execs that binary.
    OUTPUT_NAME "OpenAstro PHD2"
  )
  target_include_directories(
    phd2
    PRIVATE ${CARBON_INCLUDE_DIR}/Carbon.h
  )

  set_source_files_properties(
    ${PHD_PROJECT_ROOT_DIR}/icons/${MACOSX_BUNDLE_ICON_FILE}
      PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
  )

  set_source_files_properties(
    ${PHD_PROJECT_ROOT_DIR}/run_phd2_macos
    PROPERTIES
      MACOSX_PACKAGE_LOCATION MacOS
  )

  # Camera-specific frameworks removed - Alpaca only build.
  # libusb_openphd bundling removed alongside libusb itself (this fork's
  # cameras come over Alpaca/INDI; no direct USB access from PHD2).
  #
  # Homebrew dylib bundling (cfitsio, wxWidgets, image libs, etc.) into
  # "OpenAstro PHD2.app"/Contents/Frameworks/ happens in build-dmg.sh at packaging
  # time, not here — dev incremental builds run faster against Homebrew
  # dylibs in place, and only the redistributable .dmg needs the rewrite.

elseif(WIN32)

  set(phd2_WIN_SRC
    ${scopes_SRC}
    ${cam_SRC}
    ${guiding_SRC}
    ${phd2_SRC}
  )

  ADD_MSVC_PRECOMPILED_HEADER("phd.h" "src/precompiled_header.cpp" phd2_WIN_SRC)

  # resource file, some VC do not like the options provided by the precompiled headers
  list(APPEND phd2_WIN_SRC ${PHD_PROJECT_ROOT_DIR}/phd.rc)
  source_group(src FILES precompiled_header.cpp)

  link_directories("C:/Program Files (x86)/Visual Leak Detector/lib/Win64")
  add_executable(
    phd2
    WIN32
    ${phd2_WIN_SRC}
  )

  set_target_properties(
    phd2
    PROPERTIES
      OUTPUT_NAME phd2  # lower case on Win32
      LINK_FLAGS "/NODEFAULTLIB:libcmt.lib" #  /NODEFAULTLIB:libcmtd.lib
      LINK_FLAGS_DEBUG "/NODEFAULTLIB:libcmtd.lib /NODEFAULTLIB:msvcrt.lib"
  )

  target_include_directories(phd2 PRIVATE "C:/Program Files (x86)/Visual Leak Detector/include")

  # copy DLL files into the output directory

  set(_copyfiles_dbg ${PHD_COPY_EXTERNAL_ALL} ${PHD_COPY_EXTERNAL_DBG})
  set(_copyfiles_rel ${PHD_COPY_EXTERNAL_ALL} ${PHD_COPY_EXTERNAL_REL})
  foreach(_file IN LISTS _copyfiles_dbg _copyfiles_rel)
    if(NOT EXISTS ${_file})
      message(FATAL_ERROR "Dependency ${_file} does not exist")
    endif()
  endforeach()
  foreach(_config dbg rel)
    set(_realpaths_${_config})
    foreach(_file ${_copyfiles_${_config}})
      get_filename_component(_realpath ${_file} REALPATH)
      list(APPEND _realpaths_${_config} ${_realpath})
    endforeach()
    list(REMOVE_DUPLICATES _realpaths_${_config})
  endforeach()
  unset(_file)
  unset(_config)

  add_custom_command(
    TARGET phd2
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E
      $<$<CONFIG:Debug>:copy_if_different>
      $<$<NOT:$<CONFIG:Debug>>:true>
      ${_realpaths_dbg}
      $<TARGET_FILE_DIR:phd2>/.
    COMMAND_EXPAND_LISTS VERBATIM
    COMMENT "$<$<CONFIG:Debug>:Copy debug libs to output folder>"
  )
  add_custom_command(
    TARGET phd2
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E
      $<$<CONFIG:Release>:copy_if_different>
      $<$<NOT:$<CONFIG:Release>>:true>
      ${_realpaths_rel}
      $<TARGET_FILE_DIR:phd2>/.
    COMMAND_EXPAND_LISTS VERBATIM
    COMMENT "$<$<CONFIG:Release>:Copy release libs to output folder>"
  )
  unset(_realpaths_dbg)
  unset(_realpaths_rel)

## #########################
## Unix
elseif(UNIX)

  add_executable(
    phd2
    ${scopes_SRC}
    ${cam_SRC}
    ${guiding_SRC}
    ${phd2_SRC}
  )

  find_package( OpenCV REQUIRED )

  target_link_libraries(phd2 X11 ${OpenCV_LIBS})

  set_target_properties(
    phd2
    PROPERTIES
      OUTPUT_NAME phd2.bin
  )

else()
  message(FATAL_ERROR "Unsupported platform")
endif()

if(PHD_EXTERNAL_PROJECT_DEPENDENCIES)
  add_dependencies(phd2 ${PHD_EXTERNAL_PROJECT_DEPENDENCIES})
endif()

# properties of the project common to all platforms
target_compile_definitions(phd2 PRIVATE "${wxWidgets_DEFINITIONS}" "HAVE_TYPE_TRAITS")
target_compile_options(phd2 PRIVATE "${wxWidgets_CXX_FLAGS};")
if(APPLE)
  # suppress warnings about "overrides a member function but is not marked 'override'" with wxDECLARE_EVENT_TABLE
  target_compile_options(phd2 PRIVATE "-Wno-inconsistent-missing-override")
endif()
target_include_directories(phd2 PRIVATE ${wxWidgets_INCLUDE_DIRS})

foreach(lib ${PHD_LINK_EXTERNAL_DEBUG})
  target_link_libraries(phd2 debug ${lib})
endforeach()

foreach(lib ${PHD_LINK_EXTERNAL_RELEASE})
  target_link_libraries(phd2 optimized ${lib})
endforeach()

target_link_libraries(phd2
                      GPGuider # GP Guider (pulls in MPIIS_GP and MPIIS_GP_TOOLS transitively)
                      ${PHD_LINK_EXTERNAL})

if(WIN32)
  target_link_libraries(phd2 iphlpapi)
endif()

################################################################
#
# documentation + translation
#
################################################################

# adding dependencies on the generation of the htmls/translations
# Adding the generated documentation artifacts to the project
if(NOT TARGET ${default_locale}_html)
  message(FATAL_ERROR "The HTML target for the default locale cannot be found")
endif()

if(NOT TARGET ${target_string_extraction_from_sources})
  message(FATAL_ERROR "The translation target for the default locale cannot be found")
endif()

foreach(locale IN LISTS all_locales)
  get_zip_file(current_zip LOCALE ${locale})
  if(NOT "${current_zip}" STREQUAL "")
    add_dependencies(phd2 ${locale}_html)
  endif()

  get_translation_files(current_messages LOCALE ${locale})
  if(NOT "${current_messages}" STREQUAL "")
    if("${locale}" STREQUAL "${default_locale}")
      #set(dependent_target ${target_string_extraction_from_sources})
    else()
      #set(dependent_target ${locale}_translation)
      add_dependencies(phd2 ${locale}_translation)
    endif()
  else()
    if(NOT "${locale}" STREQUAL "${default_locale}")
      message(WARNING "The translation for locale ${locale} is not found")
    endif()
  endif()

endforeach()

## Apple bundling of documentation + translation
if(APPLE)
  # Bundle the help + locales files
  foreach(locale IN LISTS all_locales)

    if("${locale}" STREQUAL "${default_locale}")
      set(destination "Resources")
    else()
      set(destination "Resources/locale/${locale}")
    endif()

    get_zip_file(current_zip LOCALE ${locale})
    get_translation_files(current_translations LOCALE ${locale})

    # does not work well
    # set_source_files_properties(
    #  ${current_zip} ${current_translations}
    #  PROPERTIES
    #    MACOSX_PACKAGE_LOCATION ${destination})

    foreach(current_file IN LISTS current_translations current_zip)
      get_filename_component(message_file_name ${current_file} NAME)
      get_filename_component(message_file_ext ${current_file} EXT)
      if("${message_file_ext}" STREQUAL ".mo" OR "${message_file_ext}" STREQUAL ".zip")
        add_custom_command(
          TARGET phd2
          POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:phd2>/../${destination}
          COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${current_file} $<TARGET_FILE_DIR:phd2>/../${destination}/${message_file_name}
        )
      endif()
    endforeach()
  endforeach()
endif()

## Windows bundling of documentation + translation
if(WIN32)
  # here we generate the installation commands and the commands that will make the
  # build tree look like the install tree, such that users can run the application directly
  # from Visual Studio.
  foreach(current_locale IN LISTS all_locales)

    if("${current_locale}" STREQUAL "${default_locale}")
      # because of wXWidget (wxStandardPaths::IgnoreAppBuildSubDirs)
      # the paths "Debug", "Release" are discarded, and the parent path
      # is considered instead as the application directory
      # This is only for running the app from a Development environment
      # (TODO: use only for multiconfig cmake projects)
      set(build_tree_target_folder $<TARGET_FILE_DIR:phd2>/)
      set(install_options DESTINATION ${CMAKE_INSTALL_PREFIX}/)
    else()
      set(build_tree_target_folder $<TARGET_FILE_DIR:phd2>/locale/${current_locale})
      set(install_options DESTINATION ${CMAKE_INSTALL_PREFIX}/locale/${current_locale}/ OPTIONAL)
    endif()

    # installation of the document and translation files
    get_zip_file(current_zip LOCALE ${current_locale})
    get_translation_files(current_translations LOCALE ${current_locale})

    foreach(current_file IN LISTS current_translations current_zip)
      get_filename_component(message_file_basename ${current_file} NAME)
      get_filename_component(message_file_ext ${current_file} EXT)
      if("${message_file_ext}" STREQUAL ".mo" OR "${message_file_ext}" STREQUAL ".zip")
        add_custom_command(
          TARGET phd2
          POST_BUILD
             COMMAND ${CMAKE_COMMAND} -E make_directory "${build_tree_target_folder}"
             COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${current_file} "${build_tree_target_folder}/${message_file_basename}"
        )
        install(FILES       ${current_file}
                DESTINATION  ${CMAKE_INSTALL_PREFIX}/locale/${current_locale}
                OPTIONAL)
      endif()
    endforeach()

  endforeach() # for all locales
endif()

## Linux bundling of documentation + translation
if(UNIX AND NOT APPLE)

  foreach(current_locale IN LISTS all_locales)
    if("${current_locale}" STREQUAL "${default_locale}")
      set(destination share/phd2/)
      set(install_help_options DESTINATION ${CMAKE_INSTALL_PREFIX}/share/phd2/)
    else()
      set(destination share/phd2/locale/${current_locale}/)
      set(install_help_options DESTINATION ${CMAKE_INSTALL_PREFIX}/share/phd2/locale/${current_locale}/ OPTIONAL)
    endif()

    get_zip_file(current_zip LOCALE ${current_locale})
    get_translation_files(current_translations LOCALE ${current_locale})

    foreach(current_file IN LISTS current_translations current_zip)
      get_filename_component(message_file_name ${current_file} NAME)
      get_filename_component(message_file_ext ${current_file} EXT)
      if("${message_file_name}" STREQUAL "messages.mo" OR "${message_file_ext}" STREQUAL ".zip")
        add_custom_command(
          TARGET phd2
          POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:phd2>/${destination}
          COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${current_file} $<TARGET_FILE_DIR:phd2>/${destination}${message_file_name}
        )
      endif()

      if("${message_file_name}" STREQUAL "messages.mo")
          install(FILES       ${current_file}
                  DESTINATION  ${CMAKE_INSTALL_PREFIX}/share/locale/${current_locale}/LC_MESSAGES/
                  RENAME "phd2.mo"
                  OPTIONAL)
      endif()

      if("${message_file_ext}" STREQUAL ".zip")
          install(FILES ${current_file}
                  ${install_help_options})
      endif()
    endforeach()
  endforeach()
endif()

## Bundle/install embedded web portal assets
if(APPLE)
  add_custom_command(
    TARGET phd2
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:phd2>/../Resources/webui
    COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PHD_PROJECT_ROOT_DIR}/scripts/webui $<TARGET_FILE_DIR:phd2>/../Resources/webui
  )
endif()

if(WIN32)
  add_custom_command(
    TARGET phd2
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:phd2>/webui
    COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PHD_PROJECT_ROOT_DIR}/scripts/webui $<TARGET_FILE_DIR:phd2>/webui
  )
  install(DIRECTORY ${PHD_PROJECT_ROOT_DIR}/scripts/webui/ DESTINATION ${CMAKE_INSTALL_PREFIX}/webui)
endif()

if(UNIX AND NOT APPLE)
  add_custom_command(
    TARGET phd2
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:phd2>/share/phd2/webui
    COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PHD_PROJECT_ROOT_DIR}/scripts/webui $<TARGET_FILE_DIR:phd2>/share/phd2/webui
  )
  install(DIRECTORY ${PHD_PROJECT_ROOT_DIR}/scripts/webui/ DESTINATION ${CMAKE_INSTALL_PREFIX}/share/phd2/webui)
endif()

################################################################
#
# Installation and packaging
#
include(cmake_modules/PHD2Packaging.cmake)
