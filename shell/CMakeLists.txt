cmake_minimum_required(VERSION 3.16)
project(SEEK VERSION 2.0.0 LANGUAGES CXX)

# SEEK shell: native Qt Widgets UI. All job-workflow logic lives in the Python
# engine pack (../python-backend) behind seek_cpp_bridge.py.
#
# Point CMake at your Qt 6 install if it isn't found automatically, e.g.
#   cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2019_64"

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
# Optional built-in browser for importing postings from sites that block automated downloads
# (Indeed), job search and LinkedIn/Indeed sign-ins. In the Qt Maintenance Tool it is "Qt WebEngine",
# which also needs "Qt WebChannel" and "Qt Positioning" of the same Qt version.
# Not QUIET: when WebEngine is installed but can't be loaded, Qt's own warning says why.
option(SEEK_WEBENGINE "Build the built-in browser when Qt WebEngine is available" ON)
if (SEEK_WEBENGINE)
    find_package(Qt6 OPTIONAL_COMPONENTS WebEngineWidgets)
endif ()

file(GLOB_RECURSE SEEK_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.cpp")

qt_add_executable(SEEK WIN32 MACOSX_BUNDLE ${SEEK_SOURCES} resources/seek.qrc)

target_include_directories(SEEK PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(SEEK PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set(SEEK_WEBENGINE_MISSING "" CACHE INTERNAL "Qt modules the built-in browser needs but couldn't load")
if (TARGET Qt6::WebEngineWidgets)
    target_link_libraries(SEEK PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(SEEK PRIVATE SEEK_HAS_WEBENGINE)
    set(SEEK_WEBENGINE_BUILT ON CACHE INTERNAL "Built with the built-in browser")
    message(STATUS "SEEK: Qt WebEngine found - blocked job sites open in the built-in browser")
else ()
    set(SEEK_WEBENGINE_BUILT OFF CACHE INTERNAL "Built with the built-in browser")
    if (SEEK_WEBENGINE)
        # Find out which of WebEngine's own dependencies is the problem: each is a separate Qt component.
        set(_seek_missing "")
        foreach (_m WebEngineWidgets WebEngineCore WebChannel Positioning Quick QuickWidgets Qml PrintSupport Network)
            find_package(Qt6 QUIET OPTIONAL_COMPONENTS ${_m})
            if (NOT Qt6${_m}_FOUND)
                list(APPEND _seek_missing "Qt${_m}")
            endif ()
        endforeach ()
        set(SEEK_WEBENGINE_MISSING "${_seek_missing}" CACHE INTERNAL "Qt modules the built-in browser needs but couldn't load")
        message(WARNING "SEEK: Qt WebEngine could not be loaded, so the built-in browser is OFF. "
                        "Modules that failed to load: ${_seek_missing}. Add the missing ones for Qt ${Qt6_VERSION} "
                        "with the Qt Maintenance Tool (they must be the same version as Qt).")
    else ()
        message(STATUS "SEEK: built-in browser turned off (SEEK_WEBENGINE=OFF)")
    endif ()
endif ()

# Lets a development build find the engine pack in the source tree without copying it.
get_filename_component(SEEK_BACKEND_SOURCE_DIR "${PROJECT_SOURCE_DIR}/../python-backend" ABSOLUTE)
target_compile_definitions(SEEK PRIVATE
    SEEK_VERSION="${PROJECT_VERSION}"
    SEEK_BACKEND_SOURCE_DIR="${SEEK_BACKEND_SOURCE_DIR}")

if (MSVC)
    target_compile_options(SEEK PRIVATE /W4 /utf-8)
else ()
    target_compile_options(SEEK PRIVATE -Wall -Wextra)
endif ()

# `cmake --install build --prefix dist` copies the app and the engine pack:
#   dist/SEEK(.exe) + dist/python-backend/...
# On Windows the Qt runtime (and Qt WebEngine's QtWebEngineProcess.exe, resources and locales) is added by
# windeployqt, which scripts/build_windows.ps1 runs after the install — use that script for a runnable folder.
install(TARGETS SEEK RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(DIRECTORY "${SEEK_BACKEND_SOURCE_DIR}/" DESTINATION python-backend
    PATTERN "__pycache__" EXCLUDE PATTERN ".pytest_cache" EXCLUDE PATTERN "tests" EXCLUDE PATTERN ".venv" EXCLUDE)
