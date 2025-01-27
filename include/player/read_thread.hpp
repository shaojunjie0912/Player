#pragma once

#include <cmath>
#include <cstdint>
#include <player/common.hpp>
#include <player/const.hpp>
#include <player/core.hpp>
#include <player/ffmpeg.hpp>
#include <string>

// 视频线程
#include <player/video_thread.hpp>
// 音频线程
#include <player/audio_thread.hpp>

VideoState* OpenStream(std::string const& file_name);

int ReadThread(void* arg);