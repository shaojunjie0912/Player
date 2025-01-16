cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE Release)
else()
  message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

project(CutePlayer LANGUAGES CXX)

find_package(FFMPEG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

if(WIN32)
  add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES)
  if(MSVC)
    add_compile_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_WINSOCKAPI_)
  endif()
endif()

add_subdirectory(Player)
add_subdirectory(tests)
