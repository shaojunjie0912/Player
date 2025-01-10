#include <format>
#include <fstream>
#include <iostream>

extern "C" {
#include <SDL2/SDL.h>
}

// 8 字节对齐
struct AudioData {
    uint8_t* data_;     // 64 位系统指针大小为 8 字节
    uint32_t len_;      // 4 字节
    uint32_t pos_ = 0;  // 4 字节
};

// userdata: 用户数据
// stream: 存放音频数据的缓冲区
// len: 缓冲区大小
void MyAudioCallback(void* userdata, uint8_t* stream, int len) {
    AudioData* audio_data = (AudioData*)userdata;
    memset(stream, 0, len);
    int size = (len > audio_data->len_ - audio_data->pos_) ? audio_data->len_ - audio_data->pos_ : len;
    memcpy(stream, audio_data->data_ + audio_data->pos_, size);
    audio_data->pos_ += size;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pcm_file>" << std::endl;
        return -1;
    }
    char const* pcm_file = argv[1];
    std::ifstream pcm_is{pcm_file, std::ios::binary};

    int ret = SDL_Init(SDL_INIT_AUDIO);
    if (ret != 0) {
        SDL_Log("Failed to initialize SDL: %s\n", SDL_GetError());
        return -1;
    }

    AudioData audio_data;
    SDL_AudioSpec wanted_spec{
        .freq = 44100,
        .format = AUDIO_S16SYS,
        .channels = 2,
        .samples = 1024,
        .callback = MyAudioCallback,
        .userdata = &audio_data,
    };

    ret = SDL_OpenAudio(&wanted_spec, nullptr);
    if (ret != 0) {
        SDL_Log("Failed to open audio: %s\n", SDL_GetError());
        return -1;
    }

    SDL_PauseAudio(0);  // 0: play, 1: pause

    SDL_Quit();

    return 0;
}