cmake_minimum_required(VERSION 3.25)
project(WiVRn VERSION 0.0.0 LANGUAGES C CXX)

# Set default for FetchContent projects
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Default to PIC code
include(CheckPIESupported)
check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(ExternalProject)
include(GNUInstallDirs)
include("cmake/AutoOption.cmake")

option(WIVRN_USE_SYSTEM_FREETYPE "Use system FreeType" ON)
auto_option(WIVRN_USE_SYSTEM_BOOST "Use system Boost headers" AUTO)
auto_option(WIVRN_USE_SYSTEM_OPENXR "Use system OpenXR headers" AUTO)
auto_option(WIVRN_USE_SYSTEMD "Use libsystemd" AUTO)
option(WIVRN_USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)
option(WIVRN_BUILD_CLIENT "Build WiVRn client" OFF)
option(WIVRN_BUILD_SERVER "Build WiVRn server" ON)
option(WIVRN_BUILD_DASHBOARD "Build WiVRn dashboard" OFF)
option(WIVRN_BUILD_DISSECTOR "Build Wireshark dissector" OFF)
option(WIVRN_WERROR "Treat warnings as errors" OFF)

option(WIVRN_USE_NVENC "Enable nvenc (Nvidia) hardware encoder" ON)
auto_option(WIVRN_USE_VAAPI "Enable vaapi (AMD/Intel) hardware encoder" AUTO)
auto_option(WIVRN_USE_X264 "Enable x264 software encoder" AUTO)

auto_option(WIVRN_USE_PIPEWIRE "Enable pipewire backend" AUTO)
auto_option(WIVRN_USE_PULSEAUDIO "Enable pulseaudio backend" AUTO)

option(WIVRN_FEATURE_RENDERDOC "Support renderdoc" OFF)
option(WIVRN_FEATURE_SOLARXR "Enable SolarXR driver" OFF)
option(WIVRN_FEATURE_STEAMVR_LIGHTHOUSE "Enable SteamVR Lighthouse driver" OFF)

option(WIVRN_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH OFF)

option(ENABLE_COLOURED_OUTPUT "Always produce ANSI-coloured output (GNU/Clang only)." ON)

set(WIVRN_CLIENT_URL "https://github.com/WiVRn/WiVRn/releases/latest/download/WiVRn-standard-release.apk" CACHE STRING "URL used to download the client in the GUI")

if(ENABLE_COLOURED_OUTPUT)
	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		add_compile_options(-fdiagnostics-color=always)
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		add_compile_options(-fcolor-diagnostics)
	endif()
endif()

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Git)
include(VulkanVersion)

if (WIVRN_USE_SYSTEM_BOOST STREQUAL "AUTO")
    if(WIVRN_BUILD_DISSECTOR)
        find_package(Boost 1.84.0 COMPONENTS locale)
    else()
        find_package(Boost 1.75.0 COMPONENTS locale)
    endif()
elseif(WIVRN_USE_SYSTEM_BOOST STREQUAL "ON")
    if(WIVRN_BUILD_DISSECTOR)
        find_package(Boost 1.84.0 REQUIRED COMPONENTS locale)
    else()
        find_package(Boost 1.75.0 REQUIRED COMPONENTS locale)
    endif()
endif()

if (WIVRN_BUILD_SERVER)
    if (Vulkan_VERSION  VERSION_LESS "1.3.261")
        message(FATAL_ERROR "Vulkan version must be at least 1.3.261, found ${Vulkan_VERSION}")
    endif()

    if (NOT WIVRN_USE_NVENC AND NOT WIVRN_USE_VAAPI AND NOT WIVRN_USE_X264)
        message(FATAL_ERROR "No encoder selected, use at least one of WIVRN_USE_NVENC, WIVRN_USE_VAAPI or WIVRN_USE_X264")
    endif()

    if (WIVRN_USE_VAAPI STREQUAL "AUTO")
        pkg_check_modules(LIBAV IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
        pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
        if (LIBAV_FOUND AND LIBDRM_FOUND)
            set(WIVRN_USE_VAAPI ON)
        else()
            set(WIVRN_USE_VAAPI OFF)
        endif()
    elseif (WIVRN_USE_VAAPI)
        pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
        pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
    endif()

    if (WIVRN_USE_X264 STREQUAL "AUTO")
        pkg_check_modules(X264 IMPORTED_TARGET x264)
        if (X264_FOUND)
            set(WIVRN_USE_X264 ON)
        else()
            set(WIVRN_USE_X264 OFF)
        endif()
    elseif (WIVRN_USE_X264)
        pkg_check_modules(X264 REQUIRED IMPORTED_TARGET x264)
    endif()

    if (WIVRN_USE_SYSTEMD STREQUAL "AUTO")
        pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)
        if (SYSTEMD_FOUND)
            set(WIVRN_USE_SYSTEMD ON)
        else()
            set(WIVRN_USE_SYSTEMD OFF)
        endif()
    elseif (WIVRN_USE_SYSTEMD)
        pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    endif()

    if (WIVRN_USE_PIPEWIRE STREQUAL "AUTO")
        pkg_check_modules(libpipewire IMPORTED_TARGET libpipewire-0.3)
        if (libpipewire_FOUND)
            set(WIVRN_USE_PIPEWIRE ON)
        else()
            set(WIVRN_USE_PIPEWIRE OFF)
        endif()
    elseif (WIVRN_USE_PIPEWIRE)
        pkg_check_modules(libpipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)
    endif()

    if (WIVRN_USE_PULSEAUDIO STREQUAL "AUTO")
        pkg_check_modules(libpulse IMPORTED_TARGET libpulse)
        if (libpulse_FOUND)
            set(WIVRN_USE_PULSEAUDIO ON)
        else()
            set(WIVRN_USE_PULSEAUDIO OFF)
        endif()
    elseif (WIVRN_USE_PULSEAUDIO)
        pkg_check_modules(libpulse REQUIRED IMPORTED_TARGET libpulse)
    endif()

    pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client avahi-glib)
    find_package(Eigen3 REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(CLI11 REQUIRED)

    pkg_check_modules(glib2 REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)
endif()

if (WIVRN_BUILD_DASHBOARD)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network LinguistTools)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

if (WIVRN_BUILD_CLIENT AND NOT ANDROID)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
    find_package(Fontconfig REQUIRED)

    if (WIVRN_USE_SYSTEM_OPENXR STREQUAL "AUTO")
        find_package(OpenXR 1.0.26)
    elseif(WIVRN_USE_SYSTEM_OPENXR STREQUAL "ON")
        find_package(OpenXR 1.0.26 REQUIRED)
    endif()
endif()

if(ANDROID)
    set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
endif()

if (WIVRN_BUILD_DISSECTOR)
    if (WIVRN_USE_SYSTEM_OPENXR STREQUAL "AUTO")
        find_package(OpenXR 1.0.34)
    elseif(WIVRN_USE_SYSTEM_OPENXR STREQUAL "ON")
        find_package(OpenXR 1.0.34 REQUIRED)
    endif()
    find_package(Wireshark REQUIRED)
endif()

# Common dependencies
FetchContent_Declare(boostpfr      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/boostorg/pfr/archive/refs/tags/2.2.0.tar.gz)
FetchContent_Declare(boost         EXCLUDE_FROM_ALL SYSTEM URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.xz)
# Client dependencies
FetchContent_Declare(simdjson      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.10.1.tar.gz)
FetchContent_Declare(fastgltf      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.7.1.tar.gz)
FetchContent_Declare(glm           EXCLUDE_FROM_ALL SYSTEM URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)
FetchContent_Declare(openxr_loader EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.41.tar.gz)
FetchContent_Declare(spdlog        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz)
FetchContent_Declare(stb           EXCLUDE_FROM_ALL SYSTEM URL https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.zip)
FetchContent_Declare(libktx        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/v4.3.0.tar.gz)
FetchContent_Declare(implot        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/epezent/implot/archive/refs/tags/v0.16.tar.gz)
FetchContent_Declare(imgui         EXCLUDE_FROM_ALL SYSTEM URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.1.tar.gz)
# Use the docking branch of imgui to use multi-viewport
# FetchContent_Declare(imgui         EXCLUDE_FROM_ALL URL https://github.com/ocornut/imgui/archive/ce0d0ac8298ce164b5d862577e8b087d92f6e90e.zip)

# Server dependencies
FetchContent_Declare(monado
    GIT_REPOSITORY   https://gitlab.freedesktop.org/monado/monado
    # The flatpak manifest is updated automatically
    GIT_TAG          d7089f182b0514e13554e99512d63e69c30523c5
    PATCH_COMMAND    ${CMAKE_CURRENT_LIST_DIR}/patches/apply.sh ${CMAKE_CURRENT_LIST_DIR}/patches/monado/
    EXCLUDE_FROM_ALL
    )

add_subdirectory(external)
add_subdirectory(common)

if (WIVRN_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if (WIVRN_BUILD_SERVER)
    add_subdirectory(server)

    message("Selected encoders:")
    message("\tNVENC: ${WIVRN_USE_NVENC}")
    message("\tVAAPI: ${WIVRN_USE_VAAPI}")
    message("\tx264 : ${WIVRN_USE_X264}")
    message("")
    message("Audio backends:")
    message("\tPipewire  : ${WIVRN_USE_PIPEWIRE}")
    message("\tPulseaudio: ${WIVRN_USE_PULSEAUDIO}")
    message("")
    message("Optional features:")
    message("\tsystemd: ${WIVRN_USE_SYSTEMD}")
endif()

if (WIVRN_BUILD_DASHBOARD)
    add_subdirectory(dashboard)
    set_target_properties(wivrn-dashboard PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/server)
endif()

add_subdirectory(tools)

foreach(TARGET_NAME wivrn wivrn-server wivrn-dashboard wivrn-common wivrn-dissector)
    if(TARGET ${TARGET_NAME})
        target_compile_options(${TARGET_NAME} PRIVATE
            -fdiagnostics-color -Wall -Wextra -pedantic
            -Wundef
            -Wno-missing-field-initializers
            -Wno-unused-parameter
            -Wno-comment
            -Wno-unused-variable
            -Wno-sign-compare
            $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-volatile>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-deprecated-volatile -Wno-unneeded-internal-declaration -Wno-c99-extensions -Wno-unused-private-field>
        )

        if (WIVRN_WERROR)
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()

    endif()
endforeach()
