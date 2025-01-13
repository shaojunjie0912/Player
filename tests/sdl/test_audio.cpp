#include <SDL2/SDL.h>

#include <iostream>

// 音频回调函数
void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len) {
    static Uint8 *audio_pos = NULL;  // 音频数据的当前位置
    static int audio_len = 0;        // 音频数据的剩余长度

    if (audio_len == 0) {
        // 假设 userdata 是指向音频数据的指针
        audio_len = *((int *)userdata);               // 从 userdata 获取音频数据长度
        audio_pos = (Uint8 *)userdata + sizeof(int);  // 获取音频数据的起始位置
    }

    // 将音频数据写入 stream
    int len_to_copy = (len > audio_len ? audio_len : len);  // 确保不超出音频数据的剩余部分
    SDL_memcpy(stream, audio_pos, len_to_copy);             // 复制数据到输出缓冲区

    audio_pos += len_to_copy;  // 更新数据的位置
    audio_len -= len_to_copy;  // 更新剩余长度
}

int main(int argc, char *argv[]) {
    // 初始化 SDL
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 音频数据（这里使用一个简单的例子）
    Uint8 audio_data[2048];  // 假设这个数组存放了音频数据
    for (int i = 0; i < 2048; i++) {
        audio_data[i] = (Uint8)(i % 256);  // 填充一些简单的音频数据
    }

    // SDL_AudioSpec 结构体设置
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;                   // 设置采样率
    want.format = AUDIO_U8;              // 设置音频格式
    want.channels = 2;                   // 设置立体声
    want.samples = 512;                  // 设置样本大小
    want.callback = audio_callback;      // 设置回调函数
    want.userdata = (void *)audio_data;  // 设置音频数据

    // 打开音频设备
    if (SDL_OpenAudio(&want, &have) < 0) {
        std::cerr << "SDL_OpenAudio failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // 启动音频播放
    SDL_PauseAudio(0);

    // 等待音频播放结束
    SDL_Delay(5000);  // 播放 5 秒

    // 关闭音频设备
    SDL_CloseAudio();

    // 清理 SDL
    SDL_Quit();
    return 0;
}
