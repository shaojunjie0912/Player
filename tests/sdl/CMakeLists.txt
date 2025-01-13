set(TARGET "test_sdl_audio")

add_executable(${TARGET} "test_audio.cpp")

target_include_directories(${TARGET} PUBLIC ${FFMPEG_INCLUDE_DIRS}
                                            ${SDL2_INCLUDE_DIRS})

target_include_directories(${TARGET} PUBLIC include)

target_link_libraries(${TARGET} PUBLIC ${FFMPEG_LIBRARIES} SDL2::SDL2main
                                       SDL2::SDL2)
