#pragma once

#include <player/common.hpp>
#include <player/const.hpp>
#include <player/core.hpp>

int DecodeThread(void* arg);

void SdlEventLoop(VideoState* video_state);