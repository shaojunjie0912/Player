// ========================== mp4 -> aac ==========================

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

// TODO: 类似 go 中的 defer 应该如何实现? 异常退出前释放 format_ctx 内存

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

    // 从输入多媒体文件中查找输入音频流
    int audio_idx = av_find_best_stream(src_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr,
                                        0);  // HACK: av_find_best_stream <最好的流>
    if (audio_idx < 0) {
        av_log(src_format_context, AV_LOG_ERROR, "Can't find audio stream!");
        if (src_format_context) {
            avformat_close_input(&src_format_context);
        }
        return -1;
    }
    AVStream* src_audio_stream = src_format_context->streams[audio_idx];

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
    AVStream* dst_audio_stream = avformat_new_stream(dst_format_context, nullptr);
    avcodec_parameters_copy(dst_audio_stream->codecpar, src_audio_stream->codecpar);
    dst_audio_stream->codecpar->codec_tag = 0;  // 0 让 ffmpeg 自动设置

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
        if (packet->stream_index == audio_idx) {
            // 重设 pts 时基
            packet->pts = av_rescale_q_rnd(packet->pts, src_audio_stream->time_base,
                                           dst_audio_stream->time_base,
                                           (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            // 重设 dts 时基
            packet->dts = packet->pts;  // 音频 pts == dts
            packet->duration = av_rescale_q(packet->duration, src_audio_stream->time_base,
                                            dst_audio_stream->time_base);
            // 输入多媒体文件音频的流索引是 1, 输出多媒体文件只有一路音频, 因此重设为 0
            packet->stream_index = 0;
            packet->pos = -1;  // HACK: 不关心?
            // HACK: 输出到目标文件(音视频交错写入, 但其实这里只写音频orz...)
            av_interleaved_write_frame(dst_format_context, packet);
            av_packet_unref(packet);
        }
    }

    // 将多媒体<文件尾>写入输出多媒体文件
    av_write_trailer(dst_format_context);

    // 释放资源
    // TODO: go's defer
    if (src_format_context) {
        avformat_close_input(&src_format_context);
    }
    if (dst_format_context) {
        avformat_close_input(&dst_format_context);
    }

    return 0;
}
