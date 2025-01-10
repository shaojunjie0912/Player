set(TARGET main_v1)

add_executable(${TARGET} ${TARGET}.cpp)
target_include_directories(${TARGET} PRIVATE include)

target_include_directories(${TARGET} PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(${TARGET} PRIVATE ${FFMPEG_LIBRARIES} SDL2::SDL2main SDL2::SDL2 fmt::fmt)

