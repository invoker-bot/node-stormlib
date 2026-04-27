cmake_minimum_required(VERSION 3.12)
project(NodeStorm VERSION 0.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CMakeJSSetup)
include(CMakeJSNodeAddonAPI)
include(CMakeJSAddLibrary)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build vendored libraries as static libraries" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "Disable vendored StormLib tests" FORCE)
set(STORM_SKIP_INSTALL ON CACHE BOOL "Do not install vendored StormLib" FORCE)
set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "Use StormLib bundled compression libraries" FORCE)
set(STORM_UNICODE ON CACHE BOOL "Build StormLib with Unicode path support on Windows" FORCE)
add_subdirectory(third_party/StormLib EXCLUDE_FROM_ALL)

set(STORM_SOURCE_FILES main.cc)

cmakejs_add_library(${PROJECT_NAME} ${STORM_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE StormLib::storm)

if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)
endif()

