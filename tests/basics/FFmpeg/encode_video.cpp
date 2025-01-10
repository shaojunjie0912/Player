#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

// TODO: 错误处理与释放资源!

int Encode(AVCodecContext* codec_context, AVFrame* frame, AVPacket* packet, std::ofstream& os) {
    int ret = -1;
    // 发送原始视频/音频<帧> -> 编码器
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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        av_log(nullptr, AV_LOG_ERROR, "Need specify dst video file & codec name!");
        return -1;
    }
    char const* dst_file = argv[1];    // 目标文件
    char const* codec_name = argv[2];  // 编码器名称
    int ret = -1;

    // 查找 codec
    AVCodec const* codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find an codec with name '%s'!", codec_name);
        return -1;
    }

    // 创建 codec 上下文
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc codec context!");
        return -1;
    }

    // 设置视频编码器参数
    codec_context->width = 1920;       // 宽
    codec_context->height = 1080;      // 高
    codec_context->bit_rate = 500000;  // 码率

    // HACK: ⭕️ time_base * framerate = 1
    codec_context->time_base = {1, 25};  // 时基 1/25
    codec_context->framerate = {25, 1};  // 帧率 25/1

    codec_context->gop_size = 10;                 // Group of Pictures Size 图像组
    codec_context->max_b_frames = 1;              // 每个GOP中最多允许的B帧数量
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;  // 像素格式(🍩对应下面写入数据方式)

    if (codec->id == AV_CODEC_ID_H264) {  // h264 <预设> 参数
        av_opt_set(codec_context->priv_data, "preset", "slow", 0);
    }

    // 绑定编码器codec & 上下文 codec_context
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Bind codec & codec_context error!");
        return -1;
    }

    // 创建输出文件 (不使用 ffmpeg 的文件 API )
    std::ofstream os(dst_file, std::ios::binary);
    if (!os.is_open()) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file '%s'!", dst_file);
        return -1;
    }

    // 创建 AVFrame
    AVFrame* frame = av_frame_alloc();  // NOTE: 并不会为内部 data 分配空间
    if (!frame) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc frame memory error!");
        return -1;
    }
    frame->width = codec_context->width;
    frame->height = codec_context->height;
    frame->format = codec_context->pix_fmt;

    ret = av_frame_get_buffer(frame, 0);  // 给 frame->data分配空间
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc frame's data memory error!");
        return -1;
    }

    // 创建 AVPacket
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc packet memory error!");
        return -1;
    }

    // 生成视频内容
    for (int i = 0; i < 25; ++i) {
        // NOTE: 编码时会锁定 frame 中的 data 防止被其他修改
        ret = av_frame_make_writable(frame);  // 确保 frame->data 没被锁定
        if (ret < 0) {
            break;
        }
        // 对每帧图像按照 yuv 格式写入数据
        // NOTE: linesize 是平面一行的字节数, 可能 > 图像宽度 (内存对齐)
        // Y 分量
        for (int y = 0; y < codec_context->height; ++y) {
            for (int x = 0; x < codec_context->width; ++x) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }
        // UV 分量
        for (int y = 0; y < codec_context->height / 2; ++y) {
            for (int x = 0; x < codec_context->width / 2; ++x) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;  // pts

        // 编码
        ret = Encode(codec_context, frame, packet, os);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_ERROR, "Encode frame error!");
            return -1;
        }
    }
    // Encode(codec_context, nullptr, packet, os);  // HACK: 当 frame==nullptr 时强制刷新缓冲区

    // 释放资源
    if (codec_context) {
        avcodec_free_context(&codec_context);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (packet) {
        av_packet_free(&packet);
    }

    return 0;
}