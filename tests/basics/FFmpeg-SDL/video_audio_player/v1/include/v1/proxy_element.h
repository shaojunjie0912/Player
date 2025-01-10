#pragma once

#include <utility>

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

// 代理数据包
// NOTE: 其实没有必要用代理
// 直接使用 std::queue<AVPacket*> 就可以了
struct ProxyElement {
    AVPacket* packet_;

    ProxyElement(AVPacket* packet) : packet_(packet) {}
    ~ProxyElement() noexcept {
        if (packet_) {
            av_packet_free(&packet_);
        }
    }
    ProxyElement(ProxyElement const&) = delete;
    ProxyElement& operator=(ProxyElement const&) = delete;

    ProxyElement(ProxyElement&& other) noexcept { packet_ = std::exchange(other.packet_, nullptr); }
    ProxyElement& operator=(ProxyElement&& other) noexcept {
        if (this != &other) {
            if (packet_) {
                av_packet_free(&packet_);
            }
            packet_ = std::exchange(other.packet_, nullptr);
        }
        return *this;
    }
};