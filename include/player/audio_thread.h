#pragma once

#include <player/common.h>
#include <player/const.h>
#include <player/core.h>

#include <cmath>


extern "C" {
#include <libswresample/swresample.h>
}

int OpenAudio(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate);