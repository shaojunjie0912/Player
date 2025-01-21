#pragma once

#include <cmath>
#include <cstdint>
#include <string>

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

#include <player/common.hpp>
#include <player/const.hpp>
#include <player/core.hpp>

// 视频线程
#include <player/video_thread.hpp>
// 音频线程
#include <player/audio_thread.hpp>

VideoState* OpenStream(std::string const& file_name);

int ReadThread(void* arg);