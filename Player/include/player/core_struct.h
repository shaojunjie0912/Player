#pragma once

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavutil/fifo.h>
}

struct MyAVPacketList {
    /* 待解码数据 */
    AVPacket *pkt;
    /* pkt序列号 */
    int serial;
};

struct PacketQueue {
    /* ffmpeg封装的队列数据结构，里面的数据对象是MyAVPacketList */
    /* 支持操作alloc2, write, read, freep */
    AVFifo *pkt_list;
    /* 队列中当前的packet数 */
    int nb_packets;
    /* 队列所有节点占用的总内存大小 */
    int size;
    /* 队列中所有节点的合计时长 */
    int64_t duration;
    /* 终止队列操作信号，用于安全快速退出播放 */
    int abort_request;
    /* 序列号，和MyAVPacketList中的序列号作用相同，但改变的时序略有不同 */
    int serial;
    /* 互斥锁，用于保护队列操作 */
    SDL_mutex *mutex;
    /* 条件变量，用于读写进程的相互通知 */
    SDL_cond *cond;
};

struct FrameQueue {
    // Frame queue[FRAME_QUEUE_SIZE]; /* 用于存放帧数据的队列 */
    int rindex;        /* 读索引 */
    int windex;        /* 写索引 */
    int size;          /* 队列中的帧数 */
    int max_size;      /* 队列最大缓存的帧数 */
    int keep_last;     /* 播放后是否在队列中保留上一帧不销毁 */
    int rindex_shown;  /* keep_last的实现，读的时候实际上读的是rindex + rindex_shown，分析见下 */
    SDL_mutex *mutex;  /* 互斥锁，用于保护队列操作 */
    SDL_cond *cond;    /* 条件变量，用于解码和播放线程的相互通知 */
    PacketQueue *pktq; /* 指向对应的PacketQueue，FrameQueue里面的数据就是这个队列解码出来的 */
};
