macro(add_exe_sdl TARGET_NAME)
  add_executable(${TARGET_NAME} ${TARGET_NAME}.cpp)
  target_include_directories(${TARGET_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(${TARGET_NAME} PRIVATE SDL2::SDL2main SDL2::SDL2)
endmacro()

add_exe_sdl(sdl_basic1)
add_exe_sdl(sdl_basic2)
