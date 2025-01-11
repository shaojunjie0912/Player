#include <player/audio_thread.h>

int AudioDecodeFrame(VideoState* video_state) {
    int ret{-1};
    while (true) {
        // 从队列中读取数据
        // TODO: 这里 audio_packet 不是为空么?
        // 所以我手动分配
        // if (!video_state->audio_packet_) {
        //     video_state->audio_packet_ = av_packet_alloc();
        // }
        ret =
            GetPacketQueue(&video_state->audio_packet_queue_, &video_state->audio_packet_, 0);  // TODO: 这里参数视频是&
        if (ret <= 0) {
            av_log(nullptr, AV_LOG_ERROR, "GetPacketQueue failed\n");
            break;
        }
        // TODO: 音频编码器上下文不是还没传么?(我手动传了)
        ret = avcodec_send_packet(video_state->audio_codec_context_,
                                  &video_state->audio_packet_);  // TODO: 这里参数视频是&
        av_packet_unref(&video_state->audio_packet_);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "avcodec_send_packet failed\n");
            return -1;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(video_state->audio_codec_context_, &video_state->audio_frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "avcodec_receive_frame failed\n");
                return -1;
            }
            if (!video_state->audio_swr_context_) {
                AVChannelLayout in_ch_layout, out_ch_layout;
                av_channel_layout_copy(&in_ch_layout, &video_state->audio_codec_context_->ch_layout);
                av_channel_layout_copy(&out_ch_layout, &in_ch_layout);

                // 重采样
                if (video_state->audio_codec_context_->sample_fmt != AV_SAMPLE_FMT_S16) {
                    swr_alloc_set_opts2(&video_state->audio_swr_context_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                                        video_state->audio_codec_context_->sample_rate, &in_ch_layout,
                                        video_state->audio_codec_context_->sample_fmt,
                                        video_state->audio_codec_context_->sample_rate, 0, nullptr);
                    swr_init(video_state->audio_swr_context_);
                }
            }
            int data_size{0};
            if (video_state->audio_swr_context_) {
                uint8_t* const* in = static_cast<uint8_t* const*>(video_state->audio_frame_.extended_data);
                int in_count = video_state->audio_frame_.nb_samples;
                uint8_t** out = &video_state->audio_buffer_;  // TODO: audio_buffer_ 内存泄漏问题
                int out_count = video_state->audio_frame_.nb_samples + 256;

                // 重采样后输出缓冲区大小
                // = 2 * 2 * video_state->audio_frame_.nb_samples
                int out_size = av_samples_get_buffer_size(nullptr, video_state->audio_frame_.ch_layout.nb_channels,
                                                          out_count, AV_SAMPLE_FMT_S16, 0);
                // 重新分配 audio_buffer_ 内存
                av_fast_malloc(&video_state->audio_buffer_, &video_state->audio_buffer_size_, out_size);

                // 重采样 -> 返回每个通道的样本数
                int nb_ch_samples = swr_convert(video_state->audio_swr_context_, out, out_count, in, in_count);
                data_size = nb_ch_samples * video_state->audio_frame_.ch_layout.nb_channels *
                            av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            }
            // HACK: 关键 计算音频时钟
            if (!isnan(video_state->audio_frame_.pts)) {
                video_state->audio_clock_ =
                    video_state->audio_frame_.pts +
                    (double)video_state->audio_frame_.nb_samples / video_state->audio_frame_.sample_rate;
            } else {
                video_state->audio_clock_ = NAN;
            }
            av_frame_unref(&video_state->audio_frame_);
            return data_size;
        }
    }
    return 0;
}

// 回调函数(在SDL单独线程中运行)
void MyAudioCallback(void* userdata, uint8_t* stream, int len) {
    VideoState* video_state{(VideoState*)userdata};
    int len1 = 0;
    int audio_size = 0;
    while (len > 0) {
        if (video_state->audio_buffer_index_ >= video_state->audio_buffer_size_) {
            // 已经发送我们所有的数据，获取更多
            audio_size = AudioDecodeFrame(video_state);
            if (audio_size < 0) {
                // 如果出错了，输出静音
                video_state->audio_buffer_size_ = kSDLAudioBufferSize;
                video_state->audio_buffer_ = nullptr;
            } else {
                video_state->audio_buffer_size_ = audio_size;
            }
            video_state->audio_buffer_index_ = 0;
        }
        len1 = video_state->audio_buffer_size_ - video_state->audio_buffer_index_;
        if (len1 > len) {
            len1 = len;
        }
        if (video_state->audio_buffer_) {
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