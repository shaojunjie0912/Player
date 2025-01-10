extern "C" {
#include <SDL2/SDL.h>
}

const int WINDOW_WIDTH = 640;
const int WINDOW_HEIGHT = 480;

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow("Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH,
                                          WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("Failed to create window: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        SDL_Log("Failed to create renderer: %s\n", SDL_GetError());
        return -1;
    }

    // 创建纹理
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!texture) {
        SDL_Log("Failed to create texture: %s\n", SDL_GetError());
        return -1;
    }

    bool quit = false;
    SDL_Event event;
    SDL_Rect rect{.w = 30, .h = 30};
    do {
        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                quit = true;
                break;
            default:
                SDL_Log("Event type: %d\n", event.type);
        }
        rect.x = rand() % 600;
        rect.y = rand() % 450;
        SDL_SetRenderTarget(renderer, texture);          // 设置渲染目标为纹理
        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 0);  // 设置绘制颜色
        SDL_RenderClear(renderer);                       // 清空渲染目标

        SDL_RenderDrawRect(renderer, &rect);             // 绘制矩形
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 0);  // 设置绘制颜色
        SDL_RenderFillRect(renderer, &rect);             // 填充矩形

        SDL_SetRenderTarget(renderer, nullptr);               // 设置渲染目标为默认窗口
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);  // 将纹理复制到默认窗口
        SDL_RenderPresent(renderer);                          // 更新窗口显示

        // SDL_Delay(1000);

    } while (!quit);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
