#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

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

#include <v1/proxy_element.h>

// 数据包队列(保存音频)
class PacketQueue {
private:
    std::queue<ProxyElement> queue_;
    std::mutex mtx_;  // 删除了拷贝构造函数和赋值运算符
    std::condition_variable cv_;

public:
    PacketQueue() {}
    ~PacketQueue() noexcept {}
    PacketQueue(PacketQueue const&) = delete;
    PacketQueue& operator=(PacketQueue const&) = delete;
    PacketQueue(PacketQueue&&) = delete;
    PacketQueue& operator=(PacketQueue&&) = delete;

public:
    std::size_t Size() {
        std::unique_lock lk{mtx_};
        return queue_.size();
    }

public:
    void Push(AVPacket* packet) {
        AVPacket* new_packet = av_packet_alloc();  // 新的内存
        // HACK: 将 packet 的内容移动到 new_packet 中, 并将 packet 重置
        av_packet_move_ref(new_packet, packet);
        PushInternal(ProxyElement{new_packet});
    }

    // // 阻塞
    // AVPacket* Pop() {
    //     ProxyElement proxy_element = PopInternal();
    //     return proxy_element.packet_;
    // }

    // // 非阻塞
    // AVPacket* TryPop() {
    //     auto proxy_element = TryPopInternal();
    //     if (!proxy_element.has_value()) {
    //         return nullptr;
    //     } else {
    //         return proxy_element->packet_;
    //     }
    // }

    // private:
    void PushInternal(ProxyElement proxy_element) {
        std::unique_lock lk{mtx_};
        queue_.push(std::move(proxy_element));
        cv_.notify_one();
    }

    // 阻塞
    ProxyElement PopInternal() {
        std::unique_lock lk{mtx_};
        cv_.wait(lk, [this] { return !queue_.empty(); });
        ProxyElement proxy_element = std::move(queue_.front());
        queue_.pop();
        return proxy_element;
    }

    // 非阻塞
    std::optional<ProxyElement> TryPopInternal() {
        std::unique_lock lk{mtx_};
        if (queue_.empty()) {
            return std::nullopt;
        }
        ProxyElement proxy_element = std::move(queue_.front());
        queue_.pop();
        return proxy_element;
    }
};