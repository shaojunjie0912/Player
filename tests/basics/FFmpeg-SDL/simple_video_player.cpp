#include <iostream>

using std::cerr;
using std::cout;

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

// 用于传参
struct AVState {
    AVCodecContext* codec_context;
    AVFrame* frame;
    AVPacket* packet;

    SDL_Texture* texture_;
};

// 全局变量
SDL_Window* window{nullptr};      // SDL 窗口
SDL_Renderer* renderer{nullptr};  // SDL 渲染器

void Render(AVState& video_state) {
    SDL_UpdateYUVTexture(video_state.texture_, nullptr, video_state.frame->data[0], video_state.frame->linesize[0],
                         video_state.frame->data[1], video_state.frame->linesize[1], video_state.frame->data[2],
                         video_state.frame->linesize[2]);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, video_state.texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

int Decode(AVState& video_state) {
    // 发送原始视频包 -> 解码器
    int ret = avcodec_send_packet(video_state.codec_context, video_state.packet);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Send packet error!");
        return -1;
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(video_state.codec_context, video_state.frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;  //  正常退出程序(退到外部获取packet)
        } else if (ret < 0) {
            return -1;  // 异常退出程序
        }
        Render(video_state);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    av_log_set_level(AV_LOG_DEBUG);
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <video file>\n";
        return -1;
    }
    char const* file = argv[1];
    if (SDL_Init(SDL_INIT_VIDEO)) {
        cerr << "Could not initialize SDL - " << SDL_GetError() << '\n';
        return -1;
    }

    constexpr int window_width = 640;
    constexpr int window_height = 480;
    window = SDL_CreateWindow("Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        cerr << "Could not create window - " << SDL_GetError() << '\n';
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        cerr << "Could not create renderer - " << SDL_GetError() << '\n';
        return -1;
    }

    // 打开输入多媒体文件上下文
    AVFormatContext* format_context{nullptr};  // HACK: 必须初始化!!
    int ret = avformat_open_input(&format_context, file, nullptr, nullptr);
    if (avformat_open_input(&format_context, file, nullptr, nullptr) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file!");
        return -1;
    }

    // 获取流信息
    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find stream info!");
        return -1;
    }

    // 查找视频流
    int video_idx = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) {
        av_log(format_context, AV_LOG_ERROR, "Can't find video stream!");
        return -1;
    }
    AVStream* video_stream = format_context->streams[video_idx];

    // 获取解码器
    AVCodec const* video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!video_codec) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find decoder!");
        return -1;
    }

    // 创建解码器上下文
    AVCodecContext* video_codec_context = avcodec_alloc_context3(video_codec);
    if (!video_codec_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc codec context!");
        return -1;
    }

    // 从视频流中拷贝解码器参数到解码器上下文中
    avcodec_parameters_to_context(video_codec_context, video_stream->codecpar);

    // 绑定解码器和解码器上下文
    if (avcodec_open2(video_codec_context, video_codec, nullptr) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Bind codec & codec_context error!");
        return -1;
    }

    uint32_t pixel_format = SDL_PIXELFORMAT_IYUV;
    // HACK: 窗口大小是可以变化的, 但是视频分辨率是固定的
    int video_width = video_codec_context->width;
    int video_height = video_codec_context->height;
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, pixel_format, SDL_TEXTUREACCESS_STREAMING, video_width, video_height);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    SDL_Event event;
    AVState video_state{video_codec_context, frame, packet, texture};
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_idx) {
            // TODO: 解码并渲染
            Decode(video_state);
        }
        // 处理 SDL 事件
        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                return 0;
            default:
                break;
        }
        av_packet_unref(packet);
    }

    video_state.packet = nullptr;
    Decode(video_state);  // 将缓冲区中所有包解码完

    // 释放资源
    if (format_context) {
        avformat_close_input(&format_context);
    }
    if (video_codec_context) {
        avcodec_free_context(&video_codec_context);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (packet) {
        av_packet_free(&packet);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (texture) {
        SDL_DestroyTexture(texture);
    }

    SDL_Quit();
    return 0;
}