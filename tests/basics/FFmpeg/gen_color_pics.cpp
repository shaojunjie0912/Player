#include <cstdint>
#include <fstream>
#include <iostream>

using std::cout;
using std::endl;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

// NOTE: 避免编译器对结构体做额外对齐
// 使得结构体头部完全匹配 BMP 文件头的预期格式

#pragma pack(push, 1)  // HACK: 1 字节对齐(!!!!)

struct BITMAPFILEHEADER {
    uint16_t bfType;       // 2 bytes - “BM”
    uint32_t bfSize;       // 4 bytes - 文件总大小(单位:字节)
    uint16_t bfReserved1;  // 2 bytes - 保留，必须为 0
    uint16_t bfReserved2;  // 2 bytes - 保留，必须为 0
    uint32_t bfOffBits;    // 4 bytes - 图像数据偏移(单位:字节)
};

struct BITMAPINFOHEADER {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)  // HACK: 恢复默认对齐

void SaveBmpPicture(SwsContext* sws_context, AVFrame* frame, int w, int h, char* picture_name) {
    // 1. 将 YUV frame 转为 BGR24 frame
    int data_size = w * h * 3;
    AVFrame* frame_bgr24 = av_frame_alloc();  // 分配 BGR24 frame 内存
    frame_bgr24->width = w;                   // 设置 BGR24 frame 宽
    frame_bgr24->height = h;                  // 设置 BGR24 frame 高
    frame_bgr24->format = AV_PIX_FMT_BGR24;   // 设置 BGR24 frame 格式
    av_frame_get_buffer(frame_bgr24, 32);     // 分配 BGR24 frame buffer
    sws_scale(sws_context, frame->data, frame->linesize, 0, frame->height, frame_bgr24->data, frame_bgr24->linesize);

    // 2. 构造 BITMAPINFOHEADER
    BITMAPINFOHEADER bmp_info_header{};
    bmp_info_header.biSize = sizeof(BITMAPINFOHEADER);
    bmp_info_header.biWidth = w;
    bmp_info_header.biHeight = h * (-1);  // NOTE: BMP 图像数据是从左下角开始的
    bmp_info_header.biBitCount = 24;      // 24 bits -> 3 bytes -> RGB
    bmp_info_header.biPlanes = 1;         // 位平面数，为 1
    bmp_info_header.biCompression = 0;    // 不压缩

    // 3. 构造 BITMAPFILEHEADER
    BITMAPFILEHEADER bmp_file_header{};
    bmp_file_header.bfType = 0x4D42;                                                           // "BM"
    bmp_file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size;  // 文件大小
    bmp_file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);           // 数据偏移

    // 4. 将数据写入文件
    std::ofstream os{picture_name, std::ios::binary};
    os.write(reinterpret_cast<char*>(&bmp_file_header), sizeof(BITMAPFILEHEADER));  // 写入 BITMAPFILEHEADER
    os.write(reinterpret_cast<char*>(&bmp_info_header), sizeof(BITMAPINFOHEADER));  // 写入 BITMAPINFOHEADER
    os.write(reinterpret_cast<char*>(frame_bgr24->data[0]), data_size);             // 写入 BGR24 数据(都在平面 0 中)

    // 5. 释放资源
    av_frame_free(&frame_bgr24);
}

int Decode(AVCodecContext* codec_context, SwsContext* sws_context, AVFrame* frame, AVPacket* packet,
           char const* picture_name) {
    int ret = -1;
    // 发送原始视频/音频<包> -> 解码器
    ret = avcodec_send_packet(codec_context, packet);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Send packet to decoder error!");
        return -1;
    }
    while (ret >= 0) {
        // 从解码器接收解码后的<帧>
        ret = avcodec_receive_frame(codec_context, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            return -1;
        }
        char output_picture_name[1024];
        // 生成输出图片文件名(原始图片名-帧号)
        snprintf(output_picture_name, sizeof(output_picture_name), "%s-%lld.bmp", picture_name,
                 codec_context->frame_num);
        SaveBmpPicture(sws_context, frame, frame->width, frame->height, output_picture_name);
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

    // NOTE: 相比于生成无颜色的图片多了这一步! 🌈
    // 5.1 创建 SwsContext
    SwsContext* sws_context = sws_getContext(codec_context->width,   // 源宽
                                             codec_context->height,  // 源高
                                             AV_PIX_FMT_YUV420P,     // 源格式 (不能用codec_context->pix_fmt)
                                             codec_context->width,   // 目标宽
                                             codec_context->height,  // 目标高
                                             AV_PIX_FMT_BGR24,       // 目标格式
                                             SWS_BICUBIC,            // 缩放算法
                                             nullptr, nullptr, nullptr);

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
            // NOTE: 这里 Decode 函数增加了 sws_context 参数
            Decode(codec_context, sws_context, frame, packet, dst_file);
        }
    }

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

    cout << "========= End =========" << endl;
    return 0;
}
