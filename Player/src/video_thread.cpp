#include <player/video_thread.h>

extern SDL_Window* window;
extern SDL_Renderer* renderer;

int OpenVideo(VideoState* av_state) {
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

void DisplayVideo(VideoState* av_state) {
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
    CalculateDisplayRect(&rect, av_state->x_left_, av_state->y_top_, av_state->width_, av_state->height_, vp->width,
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

void VideoRefreshTimer(void* user_data) {
    VideoState* av_state = static_cast<VideoState*>(user_data);
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

void SdlEventLoop(VideoState* av_state) {
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

int QueuePicture(VideoState* av_state, AVFrame* src_frame, double pts, double duration, int64_t pos) {
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

double SyschronizeVideo(VideoState* av_state, AVFrame* frame, double pts) {
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

    VideoState* av_state = static_cast<VideoState*>(arg);
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