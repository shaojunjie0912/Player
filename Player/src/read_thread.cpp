#include <player/read_thread.h>

int OpenStreamComponent(VideoState* av_state, int stream_index) {
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

    // 线程 1: 音频(由 SDL 内部创建)
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

    }
    // 线程 2: 视频
    else if (codec_context->codec_type == AVMEDIA_TYPE_VIDEO) {
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

VideoState* OpenStream(std::string const& file_name) {
    int ret{0};
    VideoState* av_state = new VideoState();
    if (!av_state) {
        av_log(NULL, AV_LOG_ERROR, "new VideoState failed\n");
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

    // 开启读线程
    av_state->read_tid_ = SDL_CreateThread(ReadThread, "ReadThread", av_state);
    if (!av_state->read_tid_) {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread failed\n");
        return nullptr;
    }

    RefreshSchedule(av_state, 40);

    return av_state;
}

int ReadThread(void* arg) {
    int ret{-1};

    VideoState* av_state = static_cast<VideoState*>(arg);

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
