#pragma once

#include <string>

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <player/core.h>

// constexpr int kMaxAudioFrameSize = 192000;
constexpr int kVideoPictureQueueSize = 3;

enum class AVSyncType {
    AV_SYNC_AUDIO_MASTER,     // 音频为主, 视频同步到音频
    AV_SYNC_VIDEO_MASTER,     // 视频为主, 音频同步到视频
    AV_SYNC_EXTERNAL_MASTER,  // 使用外部时钟
};

struct AVState {
    std::string file_name_;
    AVFormatContext* format_context_;

    int video_stream_idx_{-1};
    int audio_stream_idx_{-1};

    // ================== Audio ==================
    AVStream* audio_stream_{nullptr};
    AVCodecContext* audio_codec_context_{nullptr};
    PacketQueue audio_packet_queue_;
    // uint8_t audio_buffer_[(kMaxAudioFrameSize * 3) / 2];// NOTE: 换成指针了
    uint8_t* audio_buffer_;
    uint32_t audio_buffer_size_;
    uint32_t audio_buffer_index_;
    AVFrame audio_frame_;
    AVPacket audio_packet_;
    uint8_t* audio_paket_data_;
    int audio_packet_size_;
    struct SwrContext* audio_swr_context_;

    // ================== Video ==================
    AVStream* video_stream_;
    AVCodecContext* video_codec_context_;
    PacketQueue video_packet_queue_;
    AVPacket video_packet_;
    struct SwsContext* video_sws_context_;

    FrameQueue video_frame_queue_;  // 解码后的视频帧队列
    int picture_queue_size_;        // 队列中的帧数
    int picture_queue_rindex_;      // 队列中读取的位置
    int picture_queue_windex_;      // 队列中写入的位置

    SDL_mutex* picture_queue_mutex_;
    SDL_cond* picture_queue_cond_;

    SDL_Thread* read_tid_;
    SDL_Thread* decode_tid_;

    // ================== Misc ==================
    int y_top{0};   // 顶部边界(窗口上面要留多少空间)
    int x_left{0};  // 左边界(窗口左边要留多少空间)

    AVSyncType av_sync_type_;

    bool quit_;

    // ================== 音视频同步 ==================
    double frame_timer_;              // 最后一帧播放的时刻(现在视频播放了多长时间)
    double frame_last_delay_;         // 最后一帧滤波延迟(上一次渲染视频帧delay时间)
    double video_current_pts_;        // 当前 pts
    int64_t video_current_pts_time_;  // 系统时间
    double frame_last_pts_;           // 上一帧的 pts

    double audio_clock_;
    double video_clock_;

    int width_;
    int height_;

    // SDL_Renderer* renderer_;
    SDL_Texture* texture_;
};