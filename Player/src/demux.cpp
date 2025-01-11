#include <cmath>
#include <cstdint>
#include <string>

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

#include <player/core.h>
#include <player/demux.h>

// TODO: 很多break存在内存泄漏隐患
// TODO: return 的话倒也轻松

constexpr int kMaxQueueSize = 15 * 1024 * 1024;
constexpr int kSDLAudioBufferSize = 1024;
constexpr int kFFRefreshEvent = SDL_USEREVENT + 1;
constexpr double kMaxAvSyncThreshold = 0.1;
constexpr double kMinAvSyncThreshold = 0.04;
constexpr double kAvNoSyncThreshold = 10.0;

constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;
extern int kDefaultWidth;
extern int kDefaultHeight;
constexpr int kScreenLeft = SDL_WINDOWPOS_CENTERED;
constexpr int kScreenTop = SDL_WINDOWPOS_CENTERED;

extern SDL_Window* window;
extern SDL_Renderer* renderer;

void CalculateDisplayRect(SDL_Rect* rect, int screen_x_left, int screen_y_top, int screen_width, int screen_height,
                          int picture_width, int picture_height, AVRational picture_sar) {
    // NOTE: picture_sar: sample aspect ratio 图片的像素宽高比(即图像每个像素的宽高比)
    // 不同于 DAR(display aspect ratio) 显示的宽高比
    AVRational aspect_ratio = picture_sar;
    int64_t width, height, x, y;

    // 如果 pic_sar 为零或负值（即无效值），则将 aspect_ratio 设置为 1:1，表示无畸变的方形像素。
    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0) {
        aspect_ratio = av_make_q(1, 1);
    }

    // 计算显示的宽高比, 根据图像原始尺寸和像素宽高比计算
    // (显示的宽高比 = 图像的宽高比 * 图像的宽度 / 图像的高度)
    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(picture_width, picture_height));

    // 计算显示的宽高
    height = screen_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > screen_width) {
        width = screen_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }

    // 计算显示的位置
    x = (screen_width - width) / 2;
    y = (screen_height - height) / 2;

    rect->x = screen_x_left + x;
    rect->y = screen_y_top + y;

    rect->w = FFMAX((int)width, 1);
    rect->h = FFMAX((int)height, 1);
}

void SetDefaultWindowSize(int width, int height, AVRational sar) {
    SDL_Rect rect;
    // int max_width = screen_width ? screen_width : INT_MAX;
    // int max_height = screen_height ? screen_height : INT_MAX;
    // if (max_width == INT_MAX && max_height == INT_MAX) max_height = height;
    int max_width = kScreenWidth;
    int max_height = kScreenHeight;
    CalculateDisplayRect(&rect, 0, 0, max_width, max_height, width, height, sar);
    // default_width = rect.w;
    // default_height = rect.h;
}

int OpenVideo(AVState* av_state) {
    int w, h;
    // w = screen_width ? screen_width : default_width;
    // h = screen_height ? screen_height : default_height;
    w = kScreenWidth;
    h = kScreenHeight;

    SDL_SetWindowTitle(window, av_state->file_name_.c_str());

    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, kScreenLeft, kScreenTop);
    SDL_ShowWindow(window);

    av_state->width_ = w;
    av_state->height_ = h;

    return 0;
}

void DisplayVideo(AVState* av_state) {
    if (!av_state->width_) {
        OpenVideo(av_state);
    }
    Frame* vp = PeekFrameQueue(&av_state->video_frame_queue_);

    AVFrame* frame = vp->frame;

    if (!av_state->texture_) {
        int width = frame->width;
        int height = frame->height;

        uint32_t pix_format = SDL_PIXELFORMAT_IYUV;
        av_state->texture_ = SDL_CreateTexture(renderer, pix_format, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!av_state->texture_) {
            av_log(nullptr, AV_LOG_ERROR, "SDL_CreateTexture failed\n");
            return;
        }
    }

    // 计算显示的位置
    SDL_Rect rect;
    CalculateDisplayRect(&rect, av_state->x_left, av_state->y_top, av_state->width_, av_state->height_, vp->width,
                         vp->height, vp->sar);

    // 渲染
    SDL_UpdateYUVTexture(av_state->texture_, nullptr, frame->data[0], frame->linesize[0], frame->data[1],
                         frame->linesize[1], frame->data[2], frame->linesize[2]);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, av_state->texture_, nullptr, &rect);
    SDL_RenderPresent(renderer);

    // 释放视频帧
    PopFrameQueue(&av_state->video_frame_queue_);  // 将已经渲染好的一帧从队列中移除
}

uint32_t MyRefreshTimerCallback(uint32_t interval, void* opaque) {
    SDL_Event event;
    event.type = kFFRefreshEvent;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);  // 插入 SDL 时间队列
    return 0;
}

void RefreshSchedule(AVState* av_state, int delay) { SDL_AddTimer(delay, MyRefreshTimerCallback, av_state); }

void VideoRefreshTimer(void* user_data) {
    AVState* av_state = static_cast<AVState*>(user_data);
    Frame* vp{nullptr};

    double actual_delay, delay, sync_threshold, ref_clock, diff;

    if (av_state->video_stream_) {                     // 如果存在视频流
        if (av_state->video_frame_queue_.size == 0) {  // 如果视频帧队列为空
            RefreshSchedule(av_state, 1);              // 快速刷新直到发现有数据
        } else {
            vp = PeekFrameQueue(&av_state->video_frame_queue_);
            av_state->video_current_pts_ = vp->pts;
            av_state->video_current_pts_time_ = av_gettime();
            if (av_state->frame_last_pts_ == 0) {
                delay = 0;
            } else {
                // the pts from last time
                delay = vp->pts - av_state->frame_last_pts_;
            }

            if (delay <= 0 || delay >= 1.0) {
                // 如果是不正确的 delay, 使用上一次的 delay
                delay = av_state->frame_last_delay_;
            }
            // save for next time
            av_state->frame_last_delay_ = delay;
            av_state->frame_last_pts_ = vp->pts;

            // 更新 delay 同步到音频
            if (av_state->av_sync_type_ != AVSyncType::AV_SYNC_AUDIO_MASTER) {
                // ref_clock = GetMasterClock(av_state);  // 获取主时钟(这里就是音频时钟)
                ref_clock = av_state->audio_clock_;  // NOTE: 我直接写死了
                diff = vp->pts - ref_clock;

                // Skip or repeat the frame. Take delay into account
                // FFPlay still doesn't "know if this is the best guess."
                sync_threshold = (delay > kMaxAvSyncThreshold) ? delay : kMaxAvSyncThreshold;
                if (fabs(diff) < kAvNoSyncThreshold) {
                    if (diff <= -sync_threshold) {        // diff 小于负阈值, 视频慢了
                        delay = 0;                        // 不要延迟，立即播放
                    } else if (diff >= sync_threshold) {  // diff 大于阈值, 视频快了
                        delay = 2 * delay;                // 延迟
                    }
                }
            }
            av_state->frame_timer_ += delay;  // 更新视频时钟
            // 计算实际延迟
            actual_delay = av_state->frame_timer_ - (av_gettime() / 1000000.0);
            if (actual_delay < 0.010) {
                actual_delay = 0.010;
            }

            RefreshSchedule(av_state, (int)(actual_delay * 1000 + 0.5));

            DisplayVideo(av_state);
        }
    } else {
        RefreshSchedule(av_state, 100);
    }
}

void SdlEventLoop(AVState* av_state) {
    SDL_Event event;
    while (true) {
        SDL_WaitEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                av_state->quit_ = true;
                SDL_Quit();
                return;
            case kFFRefreshEvent:
                // NOTE: 这里是视频刷新
                VideoRefreshTimer(event.user.data1);
                break;
            default:
                break;
        }
    }
}

int QueuePicture(AVState* av_state, AVFrame* src_frame, double pts, double duration, int64_t pos) {
    Frame* vp;
    if (!(vp = FrameQueuePeekWritable(&av_state->video_frame_queue_))) {
        return -1;
    }
    vp->sar = src_frame->sample_aspect_ratio;
    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;

    SetDefaultWindowSize(vp->width, vp->height, vp->sar);

    av_frame_move_ref(vp->frame, src_frame);
    PushFrameQueue(&av_state->video_frame_queue_);
    return 0;
}

double SyschronizeVideo(AVState* av_state, AVFrame* frame, double pts) {
    double frame_delay;

    if (pts != 0) {
        // 如果有 pts 则设置 video_clock = pts
        av_state->video_clock_ = pts;  // pts from codec
    } else {
        // 如果没有 pts 则设置 pts = video_clock
        pts = av_state->video_clock_;  // use previous frame pts
    }

    //  更新视频时钟
    frame_delay = av_q2d(av_state->video_stream_->time_base);  // 时间基 -> 秒
    // 如果我们在重复一帧，相应调整时钟
    frame_delay += frame->repeat_pict * (frame_delay * 0.5);
    av_state->video_clock_ += frame_delay;

    return pts;
}

int DecodeThread(void* arg) {
    int ret{-1};

    double pts;
    double duration;

    AVState* av_state = static_cast<AVState*>(arg);
    AVFrame* video_frame = av_frame_alloc();  // 解码后的视频帧
    Frame* frame = nullptr;

    AVRational time_base = av_state->video_stream_->time_base;
    AVRational frame_rate = av_state->video_stream_->avg_frame_rate;

    while (true) {
        if (av_state->quit_) {
            break;
        }

        // 非阻塞读取
        ret = GetPacketQueue(&av_state->video_packet_queue_, &av_state->video_packet_, 0);
        if (ret <= 0) {
            // 意味着我们停止获取包
            av_log(nullptr, AV_LOG_DEBUG, "Video delay 10 ms\n");
            SDL_Delay(10);  // 没当读不到数据就等 10 ms 再看
            continue;
        }

        ret = avcodec_send_packet(av_state->video_codec_context_, &av_state->video_packet_);
        av_packet_unref(&av_state->video_packet_);  // 清空引用计数(因为解码器内部会拷贝一份)
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "avcodec_send_packet failed\n");
            return -1;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(av_state->video_codec_context_, video_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "avcodec_receive_frame failed\n");
                return -1;
            }

            // NOTE: 如果不做音视频同步的话，这里直接显示就行了
            // DisplayVideo(av_state);

            // ================== 音视频同步 ==================
            // 计算当前帧的时长
            AVRational rational{frame_rate.den, frame_rate.num};
            duration = (frame_rate.num && frame_rate.den ? av_q2d(rational) : 0);
            pts = (video_frame->pts == AV_NOPTS_VALUE) ? NAN : video_frame->pts * av_q2d(time_base);
            pts = SyschronizeVideo(av_state, video_frame, pts);

            // 插入到视频帧队列
            QueuePicture(av_state, video_frame, pts, duration, video_frame->pkt_pos);

            // 解引用
            av_frame_unref(video_frame);
        }
    }
    return 0;
}

int AudioDecodeFrame(AVState* av_state) {
    int ret{-1};
    while (true) {
        // 从队列中读取数据
        // TODO: 这里 audio_packet 不是为空么?
        // 所以我手动分配
        // if (!av_state->audio_packet_) {
        //     av_state->audio_packet_ = av_packet_alloc();
        // }
        ret = GetPacketQueue(&av_state->audio_packet_queue_, &av_state->audio_packet_, 0);  // TODO: 这里参数视频是&
        if (ret <= 0) {
            av_log(nullptr, AV_LOG_ERROR, "GetPacketQueue failed\n");
            break;
        }
        // TODO: 音频编码器上下文不是还没传么?(我手动传了)
        ret = avcodec_send_packet(av_state->audio_codec_context_, &av_state->audio_packet_);  // TODO: 这里参数视频是&
        av_packet_unref(&av_state->audio_packet_);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "avcodec_send_packet failed\n");
            return -1;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(av_state->audio_codec_context_, &av_state->audio_frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "avcodec_receive_frame failed\n");
                return -1;
            }
            if (!av_state->audio_swr_context_) {
                AVChannelLayout in_ch_layout, out_ch_layout;
                av_channel_layout_copy(&in_ch_layout, &av_state->audio_codec_context_->ch_layout);
                av_channel_layout_copy(&out_ch_layout, &in_ch_layout);

                // 重采样
                if (av_state->audio_codec_context_->sample_fmt != AV_SAMPLE_FMT_S16) {
                    swr_alloc_set_opts2(&av_state->audio_swr_context_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                                        av_state->audio_codec_context_->sample_rate, &in_ch_layout,
                                        av_state->audio_codec_context_->sample_fmt,
                                        av_state->audio_codec_context_->sample_rate, 0, nullptr);
                    swr_init(av_state->audio_swr_context_);
                }
            }
            int data_size{0};
            if (av_state->audio_swr_context_) {
                uint8_t* const* in = static_cast<uint8_t* const*>(av_state->audio_frame_.extended_data);
                int in_count = av_state->audio_frame_.nb_samples;
                uint8_t** out = &av_state->audio_buffer_;  // TODO: audio_buffer_ 内存泄漏问题
                int out_count = av_state->audio_frame_.nb_samples + 256;

                // 重采样后输出缓冲区大小
                // = 2 * 2 * av_state->audio_frame_.nb_samples
                int out_size = av_samples_get_buffer_size(nullptr, av_state->audio_frame_.ch_layout.nb_channels,
                                                          out_count, AV_SAMPLE_FMT_S16, 0);
                // 重新分配 audio_buffer_ 内存
                av_fast_malloc(&av_state->audio_buffer_, &av_state->audio_buffer_size_, out_size);

                // 重采样 -> 返回每个通道的样本数
                int nb_ch_samples = swr_convert(av_state->audio_swr_context_, out, out_count, in, in_count);
                data_size = nb_ch_samples * av_state->audio_frame_.ch_layout.nb_channels *
                            av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            }
            // HACK: 关键 计算音频时钟
            if (!isnan(av_state->audio_frame_.pts)) {
                av_state->audio_clock_ = av_state->audio_frame_.pts +
                                         (double)av_state->audio_frame_.nb_samples / av_state->audio_frame_.sample_rate;
            } else {
                av_state->audio_clock_ = NAN;
            }
            av_frame_unref(&av_state->audio_frame_);
            return data_size;
        }
    }
    return 0;
}

// 回调函数(在SDL单独线程中运行)
void MyAudioCallback(void* userdata, uint8_t* stream, int len) {
    AVState* av_state{(AVState*)userdata};
    int len1 = 0;
    int audio_size = 0;
    while (len > 0) {
        if (av_state->audio_buffer_index_ >= av_state->audio_buffer_size_) {
            // 已经发送我们所有的数据，获取更多
            audio_size = AudioDecodeFrame(av_state);
            if (audio_size < 0) {
                // 如果出错了，输出静音
                av_state->audio_buffer_size_ = kSDLAudioBufferSize;
                av_state->audio_buffer_ = nullptr;
            } else {
                av_state->audio_buffer_size_ = audio_size;
            }
            av_state->audio_buffer_index_ = 0;
        }
        len1 = av_state->audio_buffer_size_ - av_state->audio_buffer_index_;
        if (len1 > len) {
            len1 = len;
        }
        if (av_state->audio_buffer_) {
        }
    }
}

int OpenAudio(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate) {
    SDL_AudioSpec wanted_spec, spec;
    int wanted_nb_channels{wanted_channel_layout->nb_channels};

    // 设置音频参数
    // TODO: C++20 结构体初始化
    wanted_spec.freq = wanted_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = kSDLAudioBufferSize;  // kSDLAudioBufferSize
    wanted_spec.callback = MyAudioCallback;
    wanted_spec.userdata = (void*)opaque;

    av_log(nullptr, AV_LOG_INFO, "wanted spec: channels: %d, sample_fmt: %d, sample_rate:%d\n", wanted_nb_channels,
           AUDIO_S16SYS, wanted_sample_rate);

    int ret = SDL_OpenAudio(&wanted_spec, &spec);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "SDL_OpenAudio failed\n");
        return -1;
    }
    return spec.size;
}

int OpenStreamComponent(AVState* av_state, int stream_index) {
    int ret = -1;
    int sample_rate;
    AVChannelLayout ch_layout;

    AVFormatContext* format_context{av_state->format_context_};
    if (stream_index < 0 || stream_index >= format_context->nb_streams) {
        return -1;
    }
    AVStream* stream{format_context->streams[stream_index]};
    AVCodecParameters* codec_params{stream->codecpar};

    // 查找 decoder
    AVCodec const* codec{avcodec_find_decoder(codec_params->codec_id)};
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_find_decoder failed\n");
        return -1;
    }

    // 创建 codec context
    AVCodecContext* codec_context{avcodec_alloc_context3(codec)};  // HACK: 不能轻易释放, 否则内存泄漏
    if (!codec_context) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3 failed\n");
        return -1;
    }

    // 拷贝 params -> codec context
    ret = avcodec_parameters_to_context(codec_context, codec_params);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context failed\n");
        return -1;
    }

    // 绑定 codec & codec context
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2 failed\n");
        return -1;
    }

    if (codec_context->codec_type == AVMEDIA_TYPE_AUDIO) {
        int sample_rate{codec_context->sample_rate};
        ret = av_channel_layout_copy(&ch_layout, &codec_context->ch_layout);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_channel_layout_copy failed\n");
            return -1;
        }
        // 打开扬声器
        ret = OpenAudio(av_state, &ch_layout, sample_rate);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "OpenAudio failed\n");
            return -1;
        }
        av_state->audio_buffer_size_ = 0;
        av_state->audio_buffer_index_ = 0;
        av_state->audio_stream_ = stream;
        av_state->audio_stream_idx_ = stream_index;
        av_state->audio_codec_context_ = codec_context;  // TODO: 视频里面忘记了

        // 开始播放声音
        SDL_PauseAudio(0);

    } else if (codec_context->codec_type == AVMEDIA_TYPE_VIDEO) {
        av_state->video_stream_idx_ = stream_index;
        av_state->video_stream_ = stream;
        av_state->video_codec_context_ = codec_context;  // TODO: 为什么音频编码器上下文没有存?

        // 音视频同步相关字段
        av_state->frame_timer_ = (double)av_gettime() / 1000000.0;
        av_state->frame_last_delay_ = 40e-3;
        av_state->video_current_pts_ = av_gettime();

        av_state->decode_tid_ = SDL_CreateThread(DecodeThread, "decode_thread", av_state);
    }

    // NOTE: 正常退出就不需要释放内存(因为赋值出去了)
    // 但是异常退出呢? 视频中使用 goto 的方式释放内存, 那现代C++呢?
    return 0;
}

int ReadThread(void* arg) {
    uint32_t pix_format;
    int ret{-1};

    AVState* av_state = static_cast<AVState*>(arg);

    AVFormatContext* format_context{nullptr};
    ret = avformat_open_input(&format_context, av_state->file_name_.c_str(), nullptr, nullptr);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avformat_open_input failed\n");
        return -1;
    }

    av_state->format_context_ = format_context;  // NOTE: 不能close, 否则悬空指针

    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info failed\n");
        return -1;
    }

    // 查找音频流和视频流
    for (int i{0}; i < format_context->nb_streams; ++i) {
        AVStream* stream = format_context->streams[i];
        AVCodecParameters* codecpar = stream->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_state->video_stream_idx_ = i;
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            av_state->audio_stream_idx_ = i;
        }
    }
    // 打开视频流
    // 重设视频窗口大小(这样最好, 防止分辨率不对)
    AVStream* stream{format_context->streams[av_state->video_stream_idx_]};
    AVCodecParameters* codec_params{stream->codecpar};
    AVRational sar{av_guess_sample_aspect_ratio(format_context, stream, nullptr)};
    // TODO: set default window size
    SetDefaultWindowSize(codec_params->width, codec_params->height, sar);
    OpenStreamComponent(av_state, av_state->video_stream_idx_);

    // 打开音频流
    OpenStreamComponent(av_state, av_state->audio_stream_idx_);

    AVPacket* packet{av_packet_alloc()};

    while (true) {
        // 用户退出
        if (av_state->quit_) {
            return -1;
        }

        // 限制队列大小
        if (av_state->audio_packet_queue_.size > kMaxQueueSize || av_state->video_packet_queue_.size > kMaxQueueSize) {
            SDL_Delay(10);
            continue;
        }

        // 读取包
        ret = av_read_frame(format_context, packet);
        if (ret < 0) {
            if (av_state->format_context_->pb->error == 0) {
                SDL_Delay(100);  // 没有错误, 等待用户输入
                continue;
            } else {
                break;
            }
        }

        // 保存包至队列
        if (packet->stream_index == av_state->video_stream_idx_) {
            PutPacketQueue(&av_state->video_packet_queue_, packet);  // 保存视频包
        } else if (packet->stream_index == av_state->audio_stream_idx_) {
            PutPacketQueue(&av_state->audio_packet_queue_, packet);  // 保存音频包
        } else {
            av_packet_unref(packet);  // 既不是音频流, 也不是视频流, 释放包
        }
    }
    // 等待用户关闭窗口(接收到一个 quit 消息)
    while (!av_state->quit_) {
        SDL_Delay(100);
    }

    // 释放资源
    if (packet) {
        av_packet_free(&packet);  // NOTE: 这里之所以正常释放是因为保存到队列是"复制"
    }
    return 0;
}

AVState* OpenStream(std::string const& file_name) {
    int ret{0};
    AVState* av_state = new AVState();
    if (!av_state) {
        av_log(NULL, AV_LOG_ERROR, "new AVState failed\n");
        return nullptr;
    }

    av_state->file_name_ = file_name;

    // 初始化 Video PacketQueue
    ret = InitPacketQueue(&av_state->video_packet_queue_);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Init Video PacketQueue failed\n");
        return nullptr;
    }
    // 初始化 Audio PacketQueue
    ret = InitPacketQueue(&av_state->audio_packet_queue_);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Init Audio PacketQueue failed\n");
        return nullptr;
    }

    // 初始化 Video FrameQueue
    // TODO: 最后两个形参如何设置?
    ret = InitFrameQueue(&av_state->video_frame_queue_, &av_state->video_packet_queue_, kVideoPictureQueueSize, 1);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Init Video FrameQueue failed\n");
        return nullptr;
    }

    // 设置同步方式
    av_state->av_sync_type_ = AVSyncType::AV_SYNC_AUDIO_MASTER;

    // 开启读线程
    av_state->read_tid_ = SDL_CreateThread(ReadThread, "ReadThread", av_state);
    if (!av_state->read_tid_) {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread failed\n");
        return nullptr;
    }

    RefreshSchedule(av_state, 40);

    return av_state;
}