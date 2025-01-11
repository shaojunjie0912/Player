cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(CutePlayer LANGUAGES CXX)

find_package(FFMPEG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

# 如果是windows msvc编译器，需要添加这个
if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # add_compile_definitions(NOMINMAX)
endif()

add_subdirectory(Player)
# add_subdirectory(tests)
