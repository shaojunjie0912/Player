#pragma once

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

#include <v1/packet_queue.h>

// 视频音频状态
struct AVState {
    AVCodecContext* video_codec_context_;
    AVCodecContext* audio_codec_context_;

    AVFrame* video_frame_;
    AVFrame* audio_frame_;

    AVPacket* video_packet_;
    AVPacket* audio_packet_;

    SDL_Texture* texture_;

    PacketQueue audio_queue_;      // <原始>音频数据包队列
    uint8_t* audio_buffer_;        // <解码后>的音频数据
    uint32_t audio_buffer_size_;   // <解码后>的音频数据大小
    uint32_t audio_buffer_index_;  // <已经播放>的音频数据大小

    SwrContext* swr_context_;  // 重采样上下文

    // TODO: 这里原始指针按值传递进来后没有置为 nullptr, 不是引用传递也就无法置为 nullptr
    AVState(AVCodecContext* video_codec_context, AVCodecContext* audio_codec_context, AVFrame* video_frame,
            AVFrame* audio_frame, AVPacket* video_packet, AVPacket* audio_packet, SDL_Texture* texture)
        : video_codec_context_(video_codec_context),
          audio_codec_context_(audio_codec_context),
          video_frame_(video_frame),
          audio_frame_(audio_frame),
          video_packet_(video_packet),
          audio_packet_(audio_packet),
          texture_(texture) {}

    ~AVState() noexcept {
        if (video_codec_context_) {
            avcodec_free_context(&video_codec_context_);
        }
        if (audio_codec_context_) {
            avcodec_free_context(&audio_codec_context_);
        }
        if (video_frame_) {
            av_frame_free(&video_frame_);
        }
        if (audio_frame_) {
            av_frame_free(&audio_frame_);
        }
        if (video_packet_) {
            av_packet_free(&video_packet_);
        }
        if (audio_packet_) {
            av_packet_free(&audio_packet_);
        }
        if (texture_) {
            SDL_DestroyTexture(texture_);
        }
    }
};