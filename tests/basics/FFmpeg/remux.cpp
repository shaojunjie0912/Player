#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

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

    // 创建输出文件上下文
    AVFormatContext* dst_format_context;  // 根据输出文件后缀分配格式上下文
    // HACK: avformat_alloc_output_context2 = avformat_alloc_context + av_guess_format
    avformat_alloc_output_context2(&dst_format_context, nullptr, nullptr, dst_file);
    if (!dst_format_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc dst context!");
        if (src_format_context) {
            avformat_close_input(&src_format_context);
        }
        return -1;
    }

    int src_nb_streams = src_format_context->nb_streams;
    std::vector<int> stream_map(src_nb_streams);
    int tmp_stream_id = 0;
    for (int i = 0; i < src_nb_streams; ++i) {
        AVCodecParameters* src_codec_params = src_format_context->streams[i]->codecpar;
        // ❌不是视频/音频/字幕! 跳过! 置 -1
        if (src_codec_params->codec_type != AVMEDIA_TYPE_VIDEO &&
            src_codec_params->codec_type != AVMEDIA_TYPE_AUDIO &&
            src_codec_params->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_map[i] = -1;
            continue;
        }
        // 为输出文件创建新的流
        // nullptr -> ffmpeg 根据容器格式自动选择合适的编码器
        AVStream* dst_stream = avformat_new_stream(dst_format_context, nullptr);
        // 拷贝编码器参数
        avcodec_parameters_copy(dst_stream->codecpar, src_codec_params);
        dst_stream->codecpar->codec_tag = 0;
        stream_map[i] = tmp_stream_id++;
    }

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

    // 将 <"流头"> 写入 <输出多媒体文件>
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
    AVPacket* dst_packet = av_packet_alloc();
    // 将帧数据写入包中
    // HACK: 从输入文件的帧读到的包数据存于 dst_packet 中, 后面会对其更新
    while (av_read_frame(src_format_context, dst_packet) >= 0) {
        // ❌ 如果读取到包的流索引对应输出流映射索引为 -1
        // 说明不是需要的音频/视频/字幕流的包 pass!!!
        int src_stream_idx = dst_packet->stream_index;    // 输入流索引
        int dst_stream_idx = stream_map[src_stream_idx];  // 输出流索引
        if (dst_stream_idx < 0) {
            av_packet_unref(dst_packet);
            continue;
        }
        AVStream* src_stream = src_format_context->streams[src_stream_idx];
        dst_packet->stream_index = dst_stream_idx;  // 重设输出包中流索引
        AVStream* dst_stream = dst_format_context->streams[dst_stream_idx];
        // 重设时基
        av_packet_rescale_ts(dst_packet, src_stream->time_base, dst_stream->time_base);
        dst_packet->pos = -1;
        // 将包数据交错写入输出格式上下文
        av_interleaved_write_frame(dst_format_context, dst_packet);
        // 释放包资源
        av_packet_unref(dst_packet);
    }

    // 将 <"流尾"> 写入 <输出多媒体文件>
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
