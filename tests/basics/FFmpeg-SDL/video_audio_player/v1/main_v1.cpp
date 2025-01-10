#include <iostream>

using std::cerr;

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <v1/av_state.h>
#include <v1/packet_queue.h>
#include <v1/proxy_element.h>

#define AUDIO_BUFFER_SIZE 1024

// 全局变量
SDL_Window* window{nullptr};      // SDL 窗口
SDL_Renderer* renderer{nullptr};  // SDL 渲染器

void Render(AVState& av_state);
int DecodeAudioFrame(AVState& av_state);
void MyAudioCallback(void* userdata, uint8_t* stream, int len);
int Decode(AVState& av_state);

int main(int argc, char* argv[]) {
    av_log_set_level(AV_LOG_DEBUG);
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <video file>\n";
        return -1;
    }
    char const* file = argv[1];
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
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
    int video_idx{-1};
    int audio_idx{-1};
    for (int i{0}; i < format_context->nb_streams; ++i) {
        AVCodecParameters* codec_params = format_context->streams[i]->codecpar;
        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i;
        } else if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_idx = i;
        }
    }

    if (video_idx == -1) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find video stream!");
        return -1;
    }
    AVStream* video_stream = format_context->streams[video_idx];

    if (audio_idx == -1) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find audio stream!");
        return -1;
    }
    AVStream* audio_stream = format_context->streams[audio_idx];

    // 获取解码器
    AVCodec const* video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!video_codec) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find decoder!");
        return -1;
    }

    AVCodec const* audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!audio_codec) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find decoder!");
        return -1;
    }

    // 创建解码器上下文
    AVCodecContext* video_codec_context = avcodec_alloc_context3(video_codec);
    if (!video_codec_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc codec context!");
        return -1;
    }

    AVCodecContext* audio_codec_context = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc codec context!");
        return -1;
    }

    // 从流中拷贝解码器参数到解码器上下文中
    avcodec_parameters_to_context(video_codec_context, video_stream->codecpar);
    avcodec_parameters_to_context(audio_codec_context, audio_stream->codecpar);

    // 绑定解码器和解码器上下文
    if (avcodec_open2(video_codec_context, video_codec, nullptr) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Bind codec & codec_context error!");
        return -1;
    }
    if (avcodec_open2(audio_codec_context, audio_codec, nullptr) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Bind codec & codec_context error!");
        return -1;
    }

    uint32_t pixel_format = SDL_PIXELFORMAT_IYUV;
    // HACK: 窗口大小是可以变化的, 但是视频分辨率是固定的
    int video_width = video_codec_context->width;
    int video_height = video_codec_context->height;
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, pixel_format, SDL_TEXTUREACCESS_STREAMING, video_width, video_height);

    // TODO: 这些按值传递后会变成悬空指针
    AVPacket* video_packet = av_packet_alloc();
    AVPacket* audio_packet = av_packet_alloc();
    AVFrame* video_frame = av_frame_alloc();
    AVFrame* audio_frame = av_frame_alloc();

    AVState av_state{video_codec_context, audio_codec_context, video_frame, audio_frame,
                     video_packet,        audio_packet,        texture};
    SDL_Event event;

    // 设置音频参数
    SDL_AudioSpec wanted_spec{.freq = audio_codec_context->sample_rate,
                              .format = AUDIO_S16SYS,
                              .channels = (uint8_t)audio_codec_context->ch_layout.nb_channels,
                              .silence = 0,
                              .samples = AUDIO_BUFFER_SIZE,
                              .callback = MyAudioCallback,
                              .userdata = (void*)&av_state};
    // 打开音频设备
    if (SDL_OpenAudio(&wanted_spec, nullptr) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open audio!");
        return -1;
    }
    // 开始播放音频
    SDL_PauseAudio(0);

    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_idx) {
            // TODO: 解码并渲染
            av_packet_move_ref(av_state.video_packet_, packet);
            Decode(av_state);
        } else if (packet->stream_index == audio_idx) {
            av_state.audio_queue_.Push(packet);
        } else {
            av_packet_unref(packet);
        }
        // 处理 SDL 事件
        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                return 0;
            default:
                break;
        }
        av_packet_unref(video_packet);
    }

    av_state.video_packet_ = nullptr;
    Decode(av_state);  // 将缓冲区中所有包解码完

    // 释放资源
    if (format_context) {
        avformat_close_input(&format_context);
    }
    if (video_codec_context) {
        avcodec_free_context(&video_codec_context);
    }
    if (audio_codec_context) {
        avcodec_free_context(&audio_codec_context);
    }
    if (video_frame) {
        av_frame_free(&video_frame);
    }
    if (audio_frame) {
        av_frame_free(&audio_frame);
    }
    if (video_packet) {
        av_packet_free(&video_packet);
    }
    if (audio_packet) {
        av_packet_free(&audio_packet);
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
    av_log(nullptr, AV_LOG_INFO, "Over!");
    return 0;
}

void Render(AVState& av_state) {
    auto texture = av_state.texture_;
    auto video_frame = av_state.video_frame_;
    SDL_UpdateYUVTexture(texture, nullptr, video_frame->data[0], video_frame->linesize[0], video_frame->data[1],
                         video_frame->linesize[1], video_frame->data[2], video_frame->linesize[2]);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

int DecodeAudioFrame(AVState& av_state) {
    int data_size{};
    while (true) {
        // auto packet = av_state.audio_queue_.Pop();
        auto proxy_element = av_state.audio_queue_.PopInternal();
        int ret = avcodec_send_packet(av_state.audio_codec_context_, proxy_element.packet_);
        if (ret < 0) {
            throw std::runtime_error("Send packet error!");
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(av_state.audio_codec_context_, av_state.audio_frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                throw std::runtime_error("Receive frame error!");
            }

            // 创建 swr_context_
            if (!av_state.swr_context_) {
                AVChannelLayout in_ch_layout, out_ch_layout;
                av_channel_layout_copy(&in_ch_layout, &av_state.audio_codec_context_->ch_layout);
                av_channel_layout_copy(&out_ch_layout, &in_ch_layout);

                if (av_state.audio_codec_context_->sample_fmt != AV_SAMPLE_FMT_S16) {
                    // 设置重采样参数
                    swr_alloc_set_opts2(&av_state.swr_context_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                                        av_state.audio_codec_context_->sample_rate, &in_ch_layout,
                                        av_state.audio_codec_context_->sample_fmt,
                                        av_state.audio_codec_context_->sample_rate, 0, nullptr);
                    swr_init(av_state.swr_context_);
                }
            }

            // 判断音频帧参数是否与扬声器参数一致, 若不一致则需要重采样
            if (av_state.swr_context_) {
                uint8_t* const* in = static_cast<uint8_t* const*>(av_state.audio_frame_->extended_data);
                int in_count = av_state.audio_frame_->nb_samples;
                uint8_t** out = &av_state.audio_buffer_;  // TODO: audio_buffer_ 内存泄漏问题
                int out_count = av_state.audio_frame_->nb_samples + 512;

                // 重采样后输出缓冲区大小
                int out_size = av_samples_get_buffer_size(nullptr, av_state.audio_frame_->ch_layout.nb_channels,

                                                          out_count, AV_SAMPLE_FMT_S16, 0);
                // 重新分配 audio_buffer_ 内存
                av_fast_malloc(&av_state.audio_buffer_, &av_state.audio_buffer_size_, out_size);

                // 重采样 -> 返回每个通道的样本数
                int nb_ch_samples = swr_convert(av_state.swr_context_, out, out_count, in, in_count);
                data_size = nb_ch_samples * av_state.audio_frame_->ch_layout.nb_channels *
                            av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            } else {
                av_state.audio_buffer_ = av_state.video_frame_->data[0];
                data_size = av_samples_get_buffer_size(nullptr, av_state.audio_frame_->ch_layout.nb_channels,
                                                       av_state.audio_frame_->nb_samples,
                                                       (AVSampleFormat)av_state.audio_frame_->format, 1);
            }

            // av_packet_unref(packet);  // 解除引用计数
            av_packet_unref(proxy_element.packet_);
            av_frame_unref(av_state.audio_frame_);

            return data_size;
        }
    }
}

/**
 * @brief 音频回调函数
 * @param userdata 用户数据
 * @param stream 音频数据流(NOTE: 音频设备从该流中获取数据 🧀)
 * @param len 数据长度
 */
void MyAudioCallback(void* userdata, uint8_t* stream, int len) {
    AVState* av_state = static_cast<AVState*>(userdata);
    while (len > 0) {
        // 如果<解码>音频数据缓冲区中没有数据, 需要获取新数据并解码
        if (av_state->audio_buffer_index_ >= av_state->audio_buffer_size_) {
            int decoded_audio_size = DecodeAudioFrame(*av_state);
            if (decoded_audio_size < 0) {  // 播放静默声
                av_state->audio_buffer_size_ = AUDIO_BUFFER_SIZE;
                av_state->audio_buffer_ = nullptr;
            } else {
                av_state->audio_buffer_size_ = decoded_audio_size;
            }
            av_state->audio_buffer_index_ = 0;  // buffer 中有数据, 重置索引, 从头开始播放
        }
        // 计算剩余的数据长度
        int remain_size = av_state->audio_buffer_size_ - av_state->audio_buffer_index_;
        if (len < remain_size) {
            remain_size = len;
        }
        // 拷贝数据到 stream 中
        if (av_state->audio_buffer_) {
            memcpy(stream, av_state->audio_buffer_ + av_state->audio_buffer_index_, remain_size);
        } else {
            memcpy(stream, 0, remain_size);  // 静默声
        }
        len -= remain_size;
        stream += remain_size;
        av_state->audio_buffer_index_ += remain_size;
    }
}

int Decode(AVState& av_state) {
    auto video_codec_context = av_state.video_codec_context_;
    auto video_frame = av_state.video_frame_;
    auto video_packet = av_state.video_packet_;

    // 发送原始视频包 -> 解码器
    int ret = avcodec_send_packet(video_codec_context, video_packet);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Send packet error!");
        return -1;
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(video_codec_context, video_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;  //  正常退出程序(退到外部获取packet)
        } else if (ret < 0) {
            return -1;  // 异常退出程序
        }
        Render(av_state);
    }
    return 0;
}