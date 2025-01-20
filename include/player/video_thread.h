#pragma once

extern "C" {
#include <libavutil/time.h>
}

#include <player/common.h>
#include <player/const.h>
#include <player/core.h>

int DecodeThread(void* arg);

void SdlEventLoop(VideoState* video_state);