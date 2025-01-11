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

#include <player/common.h>
#include <player/const.h>
#include <player/core.h>

// 视频线程
#include <player/video_thread.h>
// 音频线程
#include <player/audio_thread.h>

int ReadThread(void* arg);
VideoState* OpenStream(std::string const& file_name);