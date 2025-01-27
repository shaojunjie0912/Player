#pragma once

#include <cmath>
#include <player/common.hpp>
#include <player/const.hpp>
#include <player/core.hpp>
#include <player/ffmpeg.hpp>

int OpenAudio(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate);