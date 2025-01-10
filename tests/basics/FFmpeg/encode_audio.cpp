#include <cmath>
#include <fstream>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

// TODO: 错误处理与释放资源!

int Encode(AVCodecContext* codec_context, AVFrame* frame, AVPacket* packet, std::ofstream& os) {
    int ret = -1;
    // 发送原始视频/音频帧 -> 编码器
    ret = avcodec_send_frame(codec_context, frame);  // frame -> codec_context
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Send frame to encoder error!");
        return -1;
    }

    // NOTE: 编码器接收到帧 (AVFrame) 并成功编码后
    // 可能会生成多个编码数据包 (AVPacket), 因此需要多次读
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, packet);
        // AVERROR(EAGAIN): 编码器没有足够的数据编码, 返回后重新发送 frame->codec
        // AVERROR_EOF: 编码器已经编码完成
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;  // 直接返回, 拿下一帧
        } else if (ret < 0) {
            return -1;
        }

        os.write(reinterpret_cast<char const*>(packet->data), packet->size);

        av_packet_unref(packet);
    }
    return 0;
}

// 检查编码器 codec 是否支持采样格式 sample_fmt
int CheckSampleFmt(AVCodec const* codec, enum AVSampleFormat sample_fmt) {
    // HACK:[Audio sample formats](https://ffmpeg.org/doxygen/trunk/group__lavu__sampfmts.html)
    enum AVSampleFormat const* p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt) {
            return 1;
        }
        ++p;
    }

    return 0;
}

// 选择 codec 支持的最佳采样率
int SelectBestSampleRate(AVCodec const* codec) {
    int const* p = codec->supported_samplerates;
    if (!p) {  // 如果不存在 supported_samplerates 则直接设为 44100
        return 44100;
    }

    // int const* p_tmp = p;
    // while (*p_tmp++) {
    //     cout << "sample rate: " << *p_tmp << endl;
    // }

    int best_sample_rate = *p;
    while (*p++) {
        // 最接近 44100 最佳
        if (std::abs(44100 - *p) < std::abs(44100 - best_sample_rate)) {
            best_sample_rate = *p;
        }
    }
    return best_sample_rate;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        av_log(nullptr, AV_LOG_ERROR, "Need specify dst audio file!");
        return -1;
    }
    char const* dst_file = argv[1];  // 目标文件
    int ret = -1;

    // 1. 查找 codec
    // TODO: 每种音频编码器只支持一种采样格式?
    AVCodec const* codec = avcodec_find_encoder_by_name("libfdk_aac");  // 有 AV_SAMPLE_FMT_S16
    // AVCodec const* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);  // 有 AV_SAMPLE_FMT_FLTP
    if (!codec) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find an codec with name!");
        return -1;
    }

    // 2. 创建 codec 上下文
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc codec context!");
        return -1;
    }

    // 3. 设置音频编码器参数
    codec_context->bit_rate = 64000;                // HACK: 音频码率 64K
    codec_context->sample_fmt = AV_SAMPLE_FMT_S16;  // HACK: 音频采样格式

    if (!CheckSampleFmt(codec, codec_context->sample_fmt)) {
        av_log(nullptr, AV_LOG_ERROR, "Sample fmt not supported by current encoder!");
        return -1;
    }

    codec_context->sample_rate = SelectBestSampleRate(codec);  // HACK: 音频采样率

    // AV_CHANNEL_LAYOUT_STEREO: 立体声(stereo)通道布局的掩码(mask)
    // 如果是 AV_CODEC_ID_AAC 则使用 AV_CHANNEL_LAYOUT_MONO
    AVChannelLayout stereo_mask = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&codec_context->ch_layout, &stereo_mask);  // HACK: 音频通道布局

    // 4. 绑定编码器codec & 上下文 codec_context
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Bind codec & codec_context error!");
        return -1;
    }

    // 5. 创建输出文件 (不使用 ffmpeg 的文件 API )
    std::ofstream os{dst_file, std::ios::binary};
    if (!os.is_open()) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file '%s'!", dst_file);
        return -1;
    }

    // 6. 创建 AVFrame
    AVFrame* frame = av_frame_alloc();  // NOTE: 并不会为内部 data 分配空间
    if (!frame) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc frame memory error!");
        return -1;
    }

    // NOTE: 后面 av_frame_get_buffer 会根据 frame 的参数
    // 给 data 分配空间, 因此音频和视频的 frame 参数不同
    frame->nb_samples = codec_context->frame_size;
    frame->format = codec_context->sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, &codec_context->ch_layout);

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc frame's data memory error!");
        return -1;
    }

    // 7. 创建 AVPacket
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc packet memory error!");
        return -1;
    }

    // 8. 生成音频内容
    float t = 0;                                                // 时间
    float tincr = 2 * M_PI * 440 / codec_context->sample_rate;  // 步长: 2 * PI * 440 / 44100
    for (int i = 0; i < 200; ++i) {
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Current data of frame is locked!");
            return -1;
        }
        uint16_t* samples = (uint16_t*)frame->data[0];  // 如果是 AV_SAMPLE_FMT_FLTP 则是 uint32_t*
        for (int j = 0; j < codec_context->frame_size; ++j) {
            samples[2 * j] = (int)(sin(t) * 10000);  // 左声道
            samples[2 * j + 1] = samples[2 * j];     // 右声道
            t += tincr;                              //
        }
        Encode(codec_context, frame, packet, os);
    }

    // Encode(codec_context, nullptr, packet, os);

    return 0;
}