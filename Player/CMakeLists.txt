file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "include/*.h")

set(TARGET "Player")

add_executable(${TARGET} ${SOURCES})

target_include_directories(${TARGET} PUBLIC ${FFMPEG_INCLUDE_DIRS}
                                            ${SDL2_INCLUDE_DIRS})

target_include_directories(${TARGET} PUBLIC include)

target_link_libraries(${TARGET} PUBLIC ${FFMPEG_LIBRARIES} SDL2::SDL2main
                                       SDL2::SDL2)
