#pragma once

extern "C" {
#include <SDL2/SDL.h>
}
constexpr int kDefaultWidth = 640;
constexpr int kDefaultHeight = 480;
constexpr int kScreenLeft = SDL_WINDOWPOS_CENTERED;
constexpr int kScreenTop = SDL_WINDOWPOS_CENTERED;
constexpr int kVideoPictureQueueSize = 3;
constexpr int kFFRefreshEvent = SDL_USEREVENT + 1;
constexpr int kMaxQueueSize = 15 * 1024 * 1024;
constexpr int kSdlAudioBufferSize = 1024;
constexpr double kMaxAvSyncThreshold = 0.1;
constexpr double kMinAvSyncThreshold = 0.04;
constexpr double kAvNoSyncThreshold = 10.0;
constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;