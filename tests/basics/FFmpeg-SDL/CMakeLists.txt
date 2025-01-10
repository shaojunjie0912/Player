# macro(add_exe_ffmpeg_sdl TARGET_NAME)
#   add_executable(${TARGET_NAME} ${TARGET_NAME}.cpp)
#   target_include_directories(${TARGET_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
#   target_link_libraries(${TARGET_NAME} PRIVATE ${FFMPEG_LIBRARIES} SDL2::SDL2main SDL2::SDL2 fmt::fmt)
# endmacro()

# add_exe_ffmpeg_sdl(yuv_player)   
# # add_exe_ffmpeg_sdl(pcm_player)   
# add_exe_ffmpeg_sdl(simple_video_player)   

add_subdirectory(video_audio_player)


