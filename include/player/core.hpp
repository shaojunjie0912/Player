#pragma once

#include <condition_variable>
#include <mutex>
#include <player/mtx_queue.hpp>
#include <string>

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
}

constexpr int kFrameQueueSize = 16;

struct MyAVPacketList {
    AVPacket *pkt;
};

struct PacketQueue {
    AVFifo *pkt_list_; /* ffmpeg封装的队列数据结构，里面的数据对象是MyAVPacketList */
    int nb_packets_;   /* 队列中当前的packet数 */
    int size_;         /* 队列所有节点占用的总内存大小 */
    int64_t duration_; /* 队列中所有节点的合计时长 */
    std::mutex mtx_;
    std::condition_variable cv_;  // 队列是否为空的条件变量
};

struct Frame {
    AVFrame frame_;  // 指向实际的AVFrame对象，存储解码后的音视频帧数据
    // double pts_;       // 帧的显示时间戳 (Presentation Timestamp)，用于同步音视频
    // double duration_;  // 帧的显示持续时间，用于播放控制
    // int64_t pos_;      // 帧在输入流中的字节偏移量，通常用于调试和定位
    // AVRational sar_;   // 帧的采样长宽比 (Sample Aspect Ratio)，描述像素宽高比
    // int width_;        // 帧的宽度，单位为像素
    // int height_;       // 帧的高度，单位为像素
    // int format_;       // 帧的像素格式，取值为AVPixelFormat中的枚举值
    Frame() {}
    ~Frame() {
        if (frame_) {
            av_frame_free(&frame_);
        }
    }
};

struct FrameQueue {
    MtxQueue<Frame> queue_;
    std::mutex mtx_;
    std::condition_variable cv_notfull_;   // 队列是否为空的条件变量
    std::condition_variable cv_notempty_;  // 队列是否为满的条件变量
    PacketQueue *packet_queue_;            // 关联的 PacketQueue

    FrameQueue(PacketQueue *packet_queue) : packet_queue_(packet_queue) {}
    ~FrameQueue() {}
};

struct VideoState {
    std::string file_name_;
    AVFormatContext *format_context_;

    // ================== Audio & Video ==================
    int video_stream_idx_{-1};
    int audio_stream_idx_{-1};

    AVStream *audio_stream_;
    AVStream *video_stream_;

    AVCodecContext *audio_codec_context_;
    AVCodecContext *video_codec_context_;

    PacketQueue audio_packet_queue_;
    PacketQueue video_packet_queue_;

    AVPacket audio_packet_;
    AVPacket video_packet_;

    // ================== Audio ==================
    AVFrame audio_frame_;
    uint8_t *audio_buffer_;
    uint32_t audio_buffer_size_;
    uint32_t audio_buffer_index_;
    struct SwrContext *audio_swr_context_;

    // ================== Video ==================
    FrameQueue video_frame_queue_;  // 解码后的视频帧队列

    // ================== SDL ==================
    int x_left_;  // 播放器窗口左上角 x 坐标
    int y_top_;   // 播放器窗口左上角 y 坐标
    int width_;   // 播放器窗口宽度
    int height_;  // 播放器窗口高度

    SDL_Texture *texture_;

    // ================== Sync ==================
    // NOTE: 写死了主时钟为音频时钟
    double frame_timer_;              // 最后一帧播放的时刻(现在视频播放了多长时间)
    double frame_last_delay_;         // 最后一帧滤波延迟(上一次渲染视频帧delay时间)
    double video_current_pts_;        // 当前 pts
    int64_t video_current_pts_time_;  // 系统时间
    double frame_last_pts_;           // 上一帧的 pts

    double audio_clock_;
    double video_clock_;

    // ================== Misc ==================
    SDL_Thread *read_tid_;
    SDL_Thread *decode_tid_;

    SDL_cond *continue_read_thread_;

    bool quit_{false};
};

// ================== PacketQueue Functions ==================
int InitPacketQueue(PacketQueue *q);

int PutPacketQueueInternal(PacketQueue *q, AVPacket *pkt);

int PutPacketQueue(PacketQueue *q, AVPacket *pkt);

int GetPacketQueue(PacketQueue *q, AVPacket *pkt, int block);

void FlushPacketQueue(PacketQueue *q);

void DestoryPacketQueue(PacketQueue *q);

// ================== FrameQueue Functions ==================

void MoveWriteIndex(FrameQueue *f);  // 将写索引后移

void MoveReadIndex(FrameQueue *f);  // 将读索引后移

Frame *PeekWritableFrameQueue(FrameQueue *f);

Frame *PeekFrameQueue(FrameQueue *f);