// ========================== mp4 -> h264 ==========================

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

// HACK: 为什么抽取的 h264 文件的帧率是 25 帧(默认)，而源视频是 60 帧?
// 因为 h264 只保存压缩后的裸数据, 无法保存帧率

int main(int argc, char* argv[]) {
    if (argc < 3) {
        av_log(nullptr, AV_LOG_ERROR, "Need specify src & dst file!");
        return -1;
    }
    char const* src_file = argv[1];
    char const* dst_file = argv[2];

    // 打开输入多媒体文件上下文
    AVFormatContext* src_format_context = nullptr;
    int ret = avformat_open_input(&src_format_context, src_file, nullptr, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file!");
        return -1;
    }

    // 从输入多媒体文件中查找输入视频流
    int video_idx = av_find_best_stream(src_format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) {
        av_log(src_format_context, AV_LOG_ERROR, "Can't find video stream!");
        if (src_format_context) {
            avformat_close_input(&src_format_context);
        }
        return -1;
    }
    AVStream* src_video_stream = src_format_context->streams[video_idx];

    // 创建输出文件上下文
    AVFormatContext* dst_format_context = avformat_alloc_context();
    if (!dst_format_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file!");
        if (src_format_context) {
            avformat_close_input(&src_format_context);
        }
        return -1;
    }
    AVOutputFormat const* dst_format =
        av_guess_format(nullptr, dst_file, nullptr);  // HACK: 根据后缀 aac <猜一猜> <输出>文件格式
    dst_format_context->oformat = dst_format;  // 重设 <输出> 文件上下文的 format

    // 创建新音频流, 并复制输出音频参数
    AVStream* dst_video_stream = avformat_new_stream(dst_format_context, nullptr);
    avcodec_parameters_copy(dst_video_stream->codecpar, src_video_stream->codecpar);
    dst_video_stream->codecpar->codec_tag = 0;  // 0 让 ffmpeg 自动设置

    // HACK: 这里使用 ffmpeg 的文件操作, 因此需要绑定 <输出文件> & <pb:io context>
    ret = avio_open2(&dst_format_context->pb, dst_file, AVIO_FLAG_WRITE, nullptr,
                     nullptr);  // 将输出多媒体文件上下文与输出多媒体文件<io>绑定
    if (ret < 0) {
        av_log(dst_format_context, AV_LOG_ERROR, "Bind IO error!");
        if (src_format_context) {
            avformat_close_input(&src_format_context);
        }
        if (dst_format_context) {
            avformat_close_input(&dst_format_context);
        }
        return -1;
    }

    // 将多媒体<文件头>写入目标文件
    ret = avformat_write_header(dst_format_context, nullptr);
    if (ret < 0) {
        av_log(dst_format_context, AV_LOG_ERROR, "Write header error!");
        if (src_format_context) {
            avformat_close_input(&src_format_context);
        }
        if (dst_format_context) {
            avformat_close_input(&dst_format_context);
        }
        return -1;
    }

    // 从输入多媒体文件中读取音频数据到输出多媒体文件上下文
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(src_format_context, packet) >= 0) {
        // 时间戳转换
        // NOTE: pts: Presentation Time Stamp 呈现时间戳
        // 四舍五入 | 上下限
        if (packet->stream_index == video_idx) {
            packet->pts = av_rescale_q_rnd(packet->pts, src_video_stream->time_base,
                                           dst_video_stream->time_base,
                                           (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            packet->dts = av_rescale_q_rnd(
                packet->dts, src_video_stream->time_base, dst_video_stream->time_base,
                (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));  // 视频 pts != dts
            packet->duration = av_rescale_q(packet->duration, src_video_stream->time_base,
                                            dst_video_stream->time_base);
            packet->stream_index = 0;  // 重设流索引
            packet->pos = -1;
            av_interleaved_write_frame(dst_format_context, packet);
            av_packet_unref(packet);
        }
    }

    // 将多媒体<文件尾>写入输出多媒体文件
    av_write_trailer(dst_format_context);

    // 释放资源
    if (src_format_context) {
        avformat_close_input(&src_format_context);
    }
    if (dst_format_context) {
        avformat_close_input(&dst_format_context);
    }

    return 0;
}
