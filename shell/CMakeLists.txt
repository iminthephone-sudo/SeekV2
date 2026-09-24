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

file(GLOB_RECURSE SEEK_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.cpp")

qt_add_executable(SEEK WIN32 MACOSX_BUNDLE ${SEEK_SOURCES} resources/seek.qrc)

target_include_directories(SEEK PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(SEEK PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)

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

# `cmake --install build --prefix dist` produces a runnable folder:
#   dist/SEEK(.exe) + dist/python-backend/...
install(TARGETS SEEK RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(DIRECTORY "${SEEK_BACKEND_SOURCE_DIR}/" DESTINATION python-backend
    PATTERN "__pycache__" EXCLUDE PATTERN ".pytest_cache" EXCLUDE PATTERN "tests" EXCLUDE PATTERN ".venv" EXCLUDE)
