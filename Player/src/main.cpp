#include <player/av_state.h>
#include <player/demux.h>

#include <iostream>
#include <string>

using std::cout;
using std::endl;

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;

// void SDLEventLoop(AVState*);

int main(int argc, char* argv[]) {
    int ret = 0;  // 返回值

    av_log_set_level(AV_LOG_DEBUG);

    if (argc < 2) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <file>\n", argv[0]);
        return -1;
    }

    std::string input_file{argv[1]};

    // int sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    // if (SDL_Init(sdl_init_flags)) {
    //     av_log(NULL, AV_LOG_ERROR, "Could not initialize SDL - %s\n", SDL_GetError());
    //     return -1;
    // }

    // SDL_Window* window = SDL_CreateWindow("CutePlayer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kWindowWidth,
    //                                       kWindowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    // if (!window) {
    //     av_log(NULL, AV_LOG_ERROR, "Could not create window - %s\n", SDL_GetError());
    //     return -1;
    // }
    // SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    // if (!window || !renderer) {
    //     av_log(NULL, AV_LOG_ERROR, "Could not create window or renderer - %s\n", SDL_GetError());
    //     return -1;
    // }
    AVState* av_state = OpenStream(input_file);

    if (!av_state) {
        av_log(NULL, AV_LOG_ERROR, "OpenStream failed\n");
        return -1;
    }
    // SDLEventLoop(av_state);
    return 0;
}