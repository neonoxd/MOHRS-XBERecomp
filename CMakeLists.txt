# Medal of Honor: Rising Sun (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
# the release version: the executables' version resource, the log's first line and release.bat's tag
project(mohrs VERSION 0.1.0 LANGUAGES C CXX RC)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version.txt ${PROJECT_VERSION})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Derive the icons from the user's XBEs; original game artwork stays out of the repository.
function(add_game_icon target xbe section)
    set(icon ${CMAKE_CURRENT_BINARY_DIR}/icons/${target}.ico)
    set(resource ${CMAKE_CURRENT_BINARY_DIR}/icons/${target}.rc)
    add_custom_command(
        OUTPUT ${icon} ${resource}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_xbe_icon.py
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe} ${section} ${icon}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_xbe_icon.py ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        COMMENT "Extracting ${target} icon from ${xbe}"
        VERBATIM)
    set_source_files_properties(${resource} PROPERTIES OBJECT_DEPENDS ${icon})
    target_sources(${target} PRIVATE ${resource})
endfunction()

# the version resource (Properties > Details) and MOHRS_VERSION for the log
function(add_version target description)
    set(VERSION_TARGET ${target})
    set(VERSION_DESCRIPTION ${description})
    set(resource ${CMAKE_CURRENT_BINARY_DIR}/version/${target}.rc)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in ${resource} @ONLY)
    target_sources(${target} PRIVATE ${resource})
    target_compile_definitions(${target} PRIVATE MOHRS_VERSION="${PROJECT_VERSION}")
endfunction()

# the SDK: sdk/ here, two folders up inside the SDK's templates, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/TGP482/XboxXBE-Recomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

# host code shared by both images; src/game and src/coop hold each image's own hooks
file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/(game|coop)/")

# partitions as xbe_symbols wrote them into config/<config>
function(config_partitions config out)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/${config})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/${config})
        file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/config/${config} parts REGEX "^partitions *=")
        string(REGEX MATCH "[0-9]+" parts "${parts}")
    endif()
    # no config yet: build.bat writes game.toml with xbe_symbols, the change reconfigures
    if(NOT parts)
        set(parts 8)
    endif()
    set(${out} ${parts} PARENT_SCOPE)
endfunction()

# target name = GAME_EXE in build.bat, xbe = GAME_XBE. The xbes sit in assets/xbe/, so the game runs from the .iso in
# assets/: the runtime prefers a loose assets/default.xbe and would then look for the data files beside it
config_partitions(game.toml GAME_PARTITIONS)
add_xbe_image(MOHRS xbe/default.xbe game.toml ${GAME_PARTITIONS} game)
add_game_icon(MOHRS xbe/default.xbe "$$XTIMAGE")
add_version(MOHRS "Medal of Honor: Rising Sun, recompiled")

# the co-op executable, which default.xbe launches for co-op and which launches default.xbe back
config_partitions(coop.toml COOP_PARTITIONS)
add_xbe_image(MOHRSCoop xbe/moh3rdc.xbe coop.toml ${COOP_PARTITIONS} coop)
add_game_icon(MOHRSCoop xbe/moh3rdc.xbe "$$XSIMAGE")
add_version(MOHRSCoop "Medal of Honor: Rising Sun co-op, recompiled")
