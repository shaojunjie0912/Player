#include <player/core.h>

// peek: 偷看(用于 FrameQueue)
// get: 获取(用于 PacketQueue)

int InitPacketQueue(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->pkt_list = av_fifo_alloc2(1, sizeof(MyAVPacketList), AV_FIFO_FLAG_AUTO_GROW);
    if (!q->pkt_list) {
        return AVERROR(ENOMEM);
    }
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(nullptr, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(nullptr, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

int PutPacketQueueInternal(PacketQueue *q, AVPacket *pkt) {
    MyAVPacketList pkt1;
    int ret;

    pkt1.pkt = pkt;

    // 写进队列
    ret = av_fifo_write(q->pkt_list, &pkt1, 1);
    if (ret < 0) {
        return ret;
    }
    q->nb_packets++;
    q->size += pkt1.pkt->size + sizeof(pkt1);
    q->duration += pkt1.pkt->duration;
    // 通知
    SDL_CondSignal(q->cond);
    return 0;
}

int PutPacketQueue(PacketQueue *q, AVPacket *pkt) {
    int ret;
    AVPacket *pkt1{av_packet_alloc()};  // HACK: 分配新内存
    if (!pkt1) {
        av_packet_unref(pkt);
        return -1;
    }
    av_packet_move_ref(pkt1, pkt);

    SDL_LockMutex(q->mutex);
    ret = PutPacketQueueInternal(q, pkt1);
    SDL_UnlockMutex(q->mutex);

    if (ret < 0) {
        av_packet_free(&pkt1);
    }

    return ret;
}

int GetPacketQueue(PacketQueue *q, AVPacket *pkt, int block) {
    MyAVPacketList pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (av_fifo_read(q->pkt_list, &pkt1, 1) >= 0) {
            q->nb_packets--;
            q->size -= pkt1.pkt->size + sizeof(pkt1);
            q->duration -= pkt1.pkt->duration;
            av_packet_move_ref(pkt, pkt1.pkt);
            av_packet_free(&pkt1.pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

void FlushPacketQueue(PacketQueue *q) {
    MyAVPacketList pkt1;

    SDL_LockMutex(q->mutex);
    while (av_fifo_read(q->pkt_list, &pkt1, 1) >= 0) {
        av_packet_free(&pkt1.pkt);
    }
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}

void DestoryPacketQueue(PacketQueue *q) {
    FlushPacketQueue(q);
    av_fifo_freep2(&q->pkt_list);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

int InitFrameQueue(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last) {
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(nullptr, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = SDL_CreateCond())) {
        av_log(nullptr, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, kFrameQueueSize);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++) {
        // 为 FrameQueue 中的 max_size 个 Frame 分配内存
        if (!(f->queue[i].frame = av_frame_alloc())) {
            return AVERROR(ENOMEM);
        }
    }
    return 0;
}

// peek 出一个可以写的 Frame，此函数可能会阻塞。
Frame *FrameQueuePeekWritable(FrameQueue *f) {
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    return &f->queue[f->windex];
}

// 偏移读索引 rindex
// HACK: 第一次 Peek 读的时候 rindex + rindex_shown = 0 + 0
// 然后单独递增 rindex_shown 并 return
// 下一次 Peek 读的时候 rindex + rindex_shown = 0 + 1
void NextFrameQueue(FrameQueue *f) {
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    av_frame_unref(f->queue[f->rindex].frame);
    if (++f->rindex == f->max_size) {
        f->rindex = 0;
    }
    SDL_LockMutex(f->mutex);
    --f->size;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

// 偏移写索引 windex
void PushFrameQueue(FrameQueue *f) {
    if (++f->windex == f->max_size) {
        f->windex = 0;
    }
    SDL_LockMutex(f->mutex);
    ++f->size;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

// 获取当前可读取的帧，而不改变队列状态。
// 渲染线程在渲染当前帧时使用，不会修改队列状态。
Frame *PeekFrameQueue(FrameQueue *f) {
    // HACK: 读取索引 + 读取索引偏移
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}
