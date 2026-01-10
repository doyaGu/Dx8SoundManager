# =============================================================================
# Dx8SoundManager - Virtools DirectX 8 Sound Manager Plugin
# =============================================================================
cmake_minimum_required(VERSION 3.16)

project(Dx8SoundManager
        VERSION 2.1.0.14
        DESCRIPTION "Virtools DirectX 8 Sound Manager Plugin"
        LANGUAGES CXX
)

# =============================================================================
# Determine if top-level project
# =============================================================================
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    if (PROJECT_IS_TOP_LEVEL)
        set(DX8SOUND_IS_TOP_LEVEL ON)
    else ()
        set(DX8SOUND_IS_TOP_LEVEL OFF)
    endif ()
else ()
    if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(DX8SOUND_IS_TOP_LEVEL ON)
    else ()
        set(DX8SOUND_IS_TOP_LEVEL OFF)
    endif ()
endif ()

# =============================================================================
# Platform and build checks
# =============================================================================
if (DX8SOUND_IS_TOP_LEVEL AND CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Run CMake from a separate directory: cmake -B build")
endif ()

if (NOT WIN32)
    message(FATAL_ERROR "Dx8SoundManager only supports Windows.")
endif ()

# =============================================================================
# C++ standard
# =============================================================================
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 98)
endif ()
if (NOT DEFINED CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED NO)
endif ()
if (NOT DEFINED CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS YES)
endif ()

# =============================================================================
# Project options
# =============================================================================
option(DX8SOUND_BUILD_STATIC "Build static library" OFF)
option(DX8SOUND_INSTALL "Generate install target" ${DX8SOUND_IS_TOP_LEVEL})

# =============================================================================
# CMake modules
# =============================================================================
include(GNUInstallDirs)

# =============================================================================
# Top-level project settings
# =============================================================================
if (DX8SOUND_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")
    set(CMAKE_USE_RELATIVE_PATHS TRUE)
    set(CMAKE_SUPPRESS_REGENERATION TRUE)

    get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
        message(STATUS "[Dx8SoundManager] Setting build type to 'Release'")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
    endif ()

    if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
        message(STATUS "[Dx8SoundManager] Setting install directory to ${CMAKE_INSTALL_PREFIX}")
    endif ()

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()

# =============================================================================
# Compiler settings
# =============================================================================
if (MSVC)
    add_compile_definitions(
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
    )
endif ()

# =============================================================================
# Virtools SDK
# =============================================================================
if (NOT TARGET VxMath OR NOT TARGET CK2)
    set(VIRTOOLS_SDK_PATH "" CACHE PATH "Path to the Virtools SDK")
    option(VIRTOOLS_SDK_FETCH_FROM_GIT "Fetch Virtools SDK from git if not found" OFF)
    set(VIRTOOLS_SDK_FETCH_FROM_GIT_PATH "" CACHE FILEPATH "Location to download SDK")

    if (NOT VIRTOOLS_SDK_PATH)
        if (DEFINED ENV{VIRTOOLS_SDK_PATH})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK_PATH}" CACHE PATH "Path to the Virtools SDK" FORCE)
        elseif (DEFINED ENV{VIRTOOLS_SDK})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK}" CACHE PATH "Path to the Virtools SDK" FORCE)
        endif ()
    endif ()

    if (NOT VIRTOOLS_SDK_PATH)
        if (VIRTOOLS_SDK_FETCH_FROM_GIT)
            include(FetchContent)
            set(FETCHCONTENT_BASE_DIR_SAVE ${FETCHCONTENT_BASE_DIR})
            if (VIRTOOLS_SDK_FETCH_FROM_GIT_PATH)
                get_filename_component(FETCHCONTENT_BASE_DIR "${VIRTOOLS_SDK_FETCH_FROM_GIT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
            endif ()
            FetchContent_Declare(
                    Virtools_SDK
                    GIT_REPOSITORY https://github.com/doyaGu/Virtools-SDK-2.1.git
                    GIT_TAG main
            )
            message(STATUS "[Dx8SoundManager] Downloading Virtools SDK")
            FetchContent_MakeAvailable(Virtools_SDK)
            set(VIRTOOLS_SDK_PATH "${virtools_sdk_SOURCE_DIR}" CACHE PATH "Path to the Virtools SDK" FORCE)
            set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
        else ()
            message(FATAL_ERROR "Virtools SDK location was not specified. Please set VIRTOOLS_SDK_PATH or enable VIRTOOLS_SDK_FETCH_FROM_GIT.")
        endif ()
    endif ()

    find_package(VirtoolsSDK REQUIRED HINTS "${VIRTOOLS_SDK_PATH}")
endif ()

# =============================================================================
# Sources
# =============================================================================
set(DX8SOUND_SOURCES
        DxSoundManager.cpp
        Dx8SoundManager.cpp
        Dx8SoundManager.h
)

# =============================================================================
# Helper function
# =============================================================================
function(dx8sound_configure_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath dsound dxguid)
endfunction()

# =============================================================================
# Targets
# =============================================================================
add_library(Dx8SoundManager SHARED ${DX8SOUND_SOURCES} Dx8SoundManager.rc)
dx8sound_configure_target(Dx8SoundManager)
set_target_properties(Dx8SoundManager PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if (DX8SOUND_BUILD_STATIC)
    add_library(Dx8SoundManagerStatic STATIC ${DX8SOUND_SOURCES})
    dx8sound_configure_target(Dx8SoundManagerStatic)
    set_target_properties(Dx8SoundManagerStatic PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
endif ()

# =============================================================================
# Installation
# =============================================================================
if (DX8SOUND_INSTALL)
    install(TARGETS Dx8SoundManager
            RUNTIME DESTINATION Managers
            LIBRARY DESTINATION Managers
    )

    if (DX8SOUND_BUILD_STATIC AND TARGET Dx8SoundManagerStatic)
        install(TARGETS Dx8SoundManagerStatic
                ARCHIVE DESTINATION lib
        )
    endif ()
endif ()

# =============================================================================
# Configuration summary
# =============================================================================
if (DX8SOUND_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "============================================================")
    message(STATUS "Dx8SoundManager Configuration Summary")
    message(STATUS "============================================================")
    message(STATUS "  Version:              ${PROJECT_VERSION}")
    message(STATUS "  Build Type:           ${CMAKE_BUILD_TYPE}")
    if (VIRTOOLS_SDK_PATH)
        message(STATUS "  Virtools SDK:         ${VIRTOOLS_SDK_PATH}")
    endif ()
    message(STATUS "  Install:              ${DX8SOUND_INSTALL}")
    message(STATUS "  Install Prefix:       ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "============================================================")
    message(STATUS "")
endif ()
