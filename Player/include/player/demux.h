#pragma once

#include <player/av_state.h>

AVState* OpenStream(std::string const& file_name);

void SdlEventLoop(AVState* av_state);