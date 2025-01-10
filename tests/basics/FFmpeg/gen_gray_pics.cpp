#include <fstream>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

void SavePicture(unsigned char* data, int linesize, int width, int height, char const* file) {
    std::ofstream os{file, std::ios::binary};
    os << "P5" << '\n' << width << ' ' << height << '\n' << 255 << '\n';
    for (int i = 0; i < height; ++i) {
        // NOTE: 这里的 data 是 YUV420P 格式数据的 Y 分量, 只有亮度
        os.write(reinterpret_cast<char const*>(data + i * linesize), width);
    }
}

int Decode(AVCodecContext* codec_context, AVFrame* frame, AVPacket* packet, char const* picture_name) {
    int ret = -1;
    // 发送原始视频/音频<包> -> 解码器
    ret = avcodec_send_packet(codec_context, packet);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Send packet to decoder error!");
        return -1;
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(codec_context, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            return -1;
        }
        char full_picture_name[1024];
        // 生成输出图片文件名(原始图片名-帧号)
        snprintf(full_picture_name, sizeof(full_picture_name), "%s-%lld.pgm", picture_name, codec_context->frame_num);
        SavePicture(frame->data[0],      // Y 分量平面
                    frame->linesize[0],  // Y 分量行字节数
                    frame->width,        // 图像宽度
                    frame->height,       // 图像高度
                    full_picture_name);
        if (packet) {
            av_packet_unref(packet);
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        av_log(nullptr, AV_LOG_ERROR, "Need specify src & dst file!");
        return -1;
    }
    char const* src_file = argv[1];
    char const* dst_file = argv[2];

    // 1. 打开输入多媒体文件上下文
    AVFormatContext* format_context = nullptr;
    int ret = avformat_open_input(&format_context, src_file, nullptr, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file!");
        return -1;
    }

    // 2. 从输入多媒体文件中查找输入视频流
    int video_idx = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) {
        av_log(format_context, AV_LOG_ERROR, "Can't find video stream!");
        if (format_context) {
            avformat_close_input(&format_context);
        }
        return -1;
    }
    AVStream* video_stream = format_context->streams[video_idx];

    // 3. 查找解码器
    // NOTE: 原来可以通过 codec_id 查找解码器
    AVCodec const* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        av_log(nullptr, AV_LOG_ERROR, "Can't find decoder!");
        if (format_context) {
            avformat_close_input(&format_context);
        }
        return -1;
    }

    // 4. 创建解码器上下文
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc codec context!");
        if (format_context) {
            avformat_close_input(&format_context);
            return -1;
        }
    }
    // HACK: 将视频流参数拷贝到解码器上下文 🍉
    avcodec_parameters_to_context(codec_context, video_stream->codecpar);

    // 5. 绑定 codec & codec_context
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Bind codec & codec_context error!");
        if (format_context) {
            avformat_close_input(&format_context);
        }
        return -1;
    }

    // 6. 创建 AVFrame
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc frame memory error!");
        if (format_context) {
            avformat_close_input(&format_context);
        }
        return -1;
    }
    // 7. 创建 AVPacket
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        av_log(nullptr, AV_LOG_ERROR, "Alloc packet memory error!");
        if (format_context) {
            avformat_close_input(&format_context);
        }
        return -1;
    }

    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_idx) {
            Decode(codec_context, frame, packet, dst_file);
        }
    }

    // HACK: 我不确定这一步是否必须要
    // Decode(codec_context, frame, nullptr, dst_file);  // nullptr 强制解码器输出剩余帧

    // 释放资源
    if (format_context) {
        avformat_close_input(&format_context);
    }

    if (codec_context) {
        avcodec_free_context(&codec_context);
    }

    if (frame) {
        av_frame_free(&frame);
    }

    if (packet) {
        av_packet_free(&packet);
    }

    std::cout << "========= End =========\n";
    return 0;
}
