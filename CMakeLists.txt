cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(CutePlayer LANGUAGES CXX)

find_package(FFMPEG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

if(MSVC)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(_WINSOCKAPI_)
endif()

add_subdirectory(Player)
