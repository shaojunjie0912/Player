#pragma once

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
    AVFifo *pkt_list; /* ffmpeg封装的队列数据结构，里面的数据对象是MyAVPacketList */
    int nb_packets;   /* 队列中当前的packet数 */
    int size;         /* 队列所有节点占用的总内存大小 */
    int64_t duration; /* 队列中所有节点的合计时长 */
    SDL_mutex *mutex;
    SDL_cond *cond;
};

struct Frame {
    AVFrame *frame;
    double pts;      /* presentation timestamp for the frame */
    double duration; /* estimated duration of the frame */
    int64_t pos;     /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
};

struct FrameQueue {
    Frame queue[kFrameQueueSize]; /* 用于存放帧数据的队列 */
    int rindex;                   /* 读索引 */
    int windex;                   /* 写索引 */
    int size;                     /* 队列中的帧数 */
    int max_size;                 /* 队列最大缓存的帧数 */
    int keep_last;                /* 播放后是否在队列中保留上一帧不销毁 */
    int rindex_shown;             /* keep_last的实现，读的时候实际上读的是rindex + rindex_shown，分析见下 */
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;  // 关联的 PacketQueue
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
    // uint8_t audio_buffer_[(kMaxAudioFrameSize * 3) / 2];// NOTE: 换成指针了
    uint8_t *audio_buffer_;
    uint32_t audio_buffer_size_;
    uint32_t audio_buffer_index_;
    uint8_t *audio_paket_data_;
    int audio_packet_size_;
    struct SwrContext *audio_swr_context_;

    // ================== Video ==================
    struct SwsContext *video_sws_context_;

    FrameQueue video_frame_queue_;  // 解码后的视频帧队列

    // ================== SDL ==================
    int x_left_;  // 播放器窗口左上角 x 坐标
    int y_top_;   // 播放器窗口左上角 y 坐标
    int width_;   // 播放器窗口宽度
    int height_;  // 播放器窗口高度

    SDL_Texture *texture_;

    // ================== Sync(固定主音频) ==================
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
int InitFrameQueue(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);
Frame *FrameQueuePeekWritable(FrameQueue *f);

void PushFrameQueue(FrameQueue *f);
Frame *PeekFrameQueue(FrameQueue *f);
void PopFrameQueue(FrameQueue *f);
