#include <iostream>
#include <opencv2/opencv.hpp>

extern "C" {
#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

static void print_error(const char *msg, int err) {
    char buf[256];
    av_strerror(err, buf, sizeof(buf));
    std::cerr << msg << ": " << buf << "\n";
}
//这个函数用于打开摄像头，不同平台可以修改这里
static bool open_camera(AVFormatContext **in_ctx,
                                    int &video_index,
                                    int w, int h, int fps) {
    avdevice_register_all();

    AVDictionary *opt = nullptr;
    /**以下平台不同可能需要修改 */
    av_dict_set(&opt, "framerate",  std::to_string(fps).c_str(), 0);
    av_dict_set(&opt, "video_size", (std::to_string(w) + "x" + std::to_string(h)).c_str(), 0);
    av_dict_set(&opt, "pixel_format", "yuyv422", 0);
    //mac 平台这样写，其他平台修改这里
    int ret = avformat_open_input(in_ctx, "0", av_find_input_format("avfoundation"), &opt);
    av_dict_free(&opt);
    if (ret < 0) { print_error("avformat_open_input", ret); return false; }

    ret = avformat_find_stream_info(*in_ctx, nullptr);
    if (ret < 0) { print_error("avformat_find_stream_info", ret); return false; }

    video_index = -1;
    for (unsigned i = 0; i < (*in_ctx)->nb_streams; ++i) {
        if ((*in_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = (int)i;
            break;
        }
    }
    if (video_index < 0) { std::cerr << "no video stream\n"; return false; }
    return true;
}

/**编码器配置
 * 
 */
static bool open_x264_encoder(AVCodecContext **enc_ctx,
                              int w, int h, int fps, int bitrate) {
    const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) { std::cerr << "no libx264\n"; return false; }

    *enc_ctx = avcodec_alloc_context3(encoder);
    if (!*enc_ctx) { std::cerr << "alloc enc_ctx fail\n"; return false; }

    AVCodecContext *c = *enc_ctx;
    c->width = w;
    c->height = h;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    // 关键：time_base 表示 pts 的“单位”
    c->time_base = AVRational{1, fps};
    c->framerate = AVRational{fps, 1};

    c->bit_rate = bitrate;

    // 推流常用：别搞B帧，低延迟
    c->max_b_frames = 0;
    c->gop_size = fps;     // 1秒一个关键帧(可按需调)
    c->keyint_min = fps;

    av_opt_set(c->priv_data, "tune", "zerolatency", 0);
    av_opt_set(c->priv_data, "preset", "veryfast", 0);

    int ret = avcodec_open2(c, encoder, nullptr);
    if (ret < 0) { print_error("avcodec_open2", ret); return false; }
    return true;
}

static void close_rtsp_output(AVFormatContext **out_ctx) {
    if (!out_ctx || !*out_ctx) return;
    av_write_trailer(*out_ctx);
    if ((*out_ctx)->pb) avio_closep(&(*out_ctx)->pb);
    avformat_free_context(*out_ctx);
    *out_ctx = nullptr;
}

static bool open_rtsp_output(AVFormatContext **out_ctx,
                             AVStream **out_stream,
                             AVCodecContext *enc_ctx,
                             const char *url) {
    // 释放旧的（用于重连）
    close_rtsp_output(out_ctx);

    int ret = avformat_alloc_output_context2(out_ctx, nullptr, "rtsp", url);
    if (ret < 0 || !*out_ctx) { print_error("alloc_output_context2", ret); return false; }

    *out_stream = avformat_new_stream(*out_ctx, nullptr);
    if (!*out_stream) { std::cerr << "new_stream fail\n"; return false; }

    // 某些容器/协议需要 global header（RTSP/SDP 经常要）
    if ((*out_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_parameters_from_context((*out_stream)->codecpar, enc_ctx);
    if (ret < 0) { print_error("parameters_from_context", ret); return false; }

    (*out_stream)->time_base = enc_ctx->time_base;

    AVDictionary *rtsp_opts = nullptr;
    av_dict_set(&rtsp_opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&rtsp_opts, "stimeout", "3000000", 0); // 3秒超时
    ret = avformat_write_header(*out_ctx, &rtsp_opts);
    av_dict_free(&rtsp_opts);

    if (ret < 0) { print_error("avformat_write_header", ret); return false; }

    std::cerr << "[rtsp] connected\n";
    return true;
}

static bool encode_and_send_one_frame(AVCodecContext *enc_ctx,
                                      AVFormatContext *out_ctx,
                                      AVStream *out_stream,
                                      SwsContext *bgr2yuv,
                                      const cv::Mat &bgr,
                                      AVFrame *yuv_frame,
                                      int64_t &pts) {
    // 确保 yuv_frame 有独立buffer，并且可写
    int ret = av_frame_make_writable(yuv_frame);
    if (ret < 0) { print_error("av_frame_make_writable", ret); return false; }

    uint8_t *src_data[4] = { (uint8_t*)bgr.data, nullptr, nullptr, nullptr };
    int src_linesize[4] = { (int)bgr.step[0], 0, 0, 0 };

    ret = sws_scale(bgr2yuv, src_data, src_linesize, 0, enc_ctx->height,
                    yuv_frame->data, yuv_frame->linesize);
    if (ret <= 0) { std::cerr << "sws_scale bgr->yuv fail\n"; return false; }

    yuv_frame->pts = pts++;  // 让时间一直递增，不要归零

    ret = avcodec_send_frame(enc_ctx, yuv_frame);
    if (ret < 0) { print_error("avcodec_send_frame", ret); return false; }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) return false;

    while (true) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) { print_error("avcodec_receive_packet", ret); av_packet_free(&pkt); return false; }

        pkt->stream_index = out_stream->index;

        // 编码器 time_base -> 流 time_base（通常相同，但写法要规范）
        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);

        ret = av_interleaved_write_frame(out_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) { print_error("av_interleaved_write_frame", ret); av_packet_free(&pkt); return false; }
    }

    av_packet_free(&pkt);
    return true;
}

int main() {
    avformat_network_init();

    const int W = 640, H = 480, FPS = 30;
    const char *RTSP_URL = "rtsp://127.0.0.1:8554/live";
    
    AVFormatContext *in_ctx = nullptr;
    int video_index = -1;
    
    if (!open_camera(&in_ctx, video_index, W, H, FPS )) return -1;

    // 输入像素格式（摄像头给的）
    AVCodecParameters *in_par = in_ctx->streams[video_index]->codecpar;
    auto in_pix = (AVPixelFormat)in_par->format;

    AVCodecContext *enc_ctx = nullptr;
    if (!open_x264_encoder(&enc_ctx, W, H, FPS, 800000)) return -1;

    AVFormatContext *out_ctx = nullptr;
    AVStream *out_stream = nullptr;

    // 第一次连接
    while (!open_rtsp_output(&out_ctx, &out_stream, enc_ctx, RTSP_URL)) {
        usleep(500 * 1000);
    }

    // sws: 摄像头YUYV -> BGR（给OpenCV）
    SwsContext *yuyv2bgr = sws_getContext(W, H, in_pix, W, H, AV_PIX_FMT_BGR24,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);

    // sws: BGR -> YUV420P（给编码器）
    SwsContext *bgr2yuv = sws_getContext(W, H, AV_PIX_FMT_BGR24, W, H, AV_PIX_FMT_YUV420P,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!yuyv2bgr || !bgr2yuv) { std::cerr << "sws_getContext fail\n"; return -1; }

    cv::Mat bgr(H, W, CV_8UC3);
    uint8_t *bgr_data[4] = { bgr.data, nullptr, nullptr, nullptr };
    int bgr_linesize[4] = { (int)bgr.step[0], 0, 0, 0 };

    // yuv_frame: 给编码器用，必须有自己的buffer
    AVFrame *yuv_frame = av_frame_alloc();
    yuv_frame->format = enc_ctx->pix_fmt;
    yuv_frame->width  = enc_ctx->width;
    yuv_frame->height = enc_ctx->height;
    if (av_frame_get_buffer(yuv_frame, 32) < 0) { std::cerr << "get_buffer fail\n"; return -1; }

    AVPacket *cam_pkt = av_packet_alloc();
    int64_t pts = 0;

    while (true) {
        int ret = av_read_frame(in_ctx, cam_pkt);
        if (ret == AVERROR(EAGAIN)) { usleep(1000); continue; }
        if (ret < 0) { print_error("av_read_frame", ret); break; }
        if (cam_pkt->stream_index != video_index) { av_packet_unref(cam_pkt); continue; }

        
        uint8_t *src_data[4] = { cam_pkt->data, nullptr, nullptr, nullptr };
        int src_linesize[4] = { W * 2, 0, 0, 0 };

        int ok = sws_scale(yuyv2bgr, src_data, src_linesize, 0, H, bgr_data, bgr_linesize);
        av_packet_unref(cam_pkt);
        if (ok <= 0) { std::cerr << "sws yuyv->bgr fail\n"; continue; }

        // ===== OpenCV处理 =====
        cv::rectangle(bgr, cv::Rect(100, 100, 200, 150), cv::Scalar(0,255,0));

        // ===== 编码+推流（失败就重连）=====
        if (!encode_and_send_one_frame(enc_ctx, out_ctx, out_stream, bgr2yuv, bgr, yuv_frame, pts)) {
            std::cerr << "[rtsp] write failed, reconnect...\n";
            while (!open_rtsp_output(&out_ctx, &out_stream, enc_ctx, RTSP_URL)) {
                usleep(500 * 1000);
            }
        }
    }

    av_packet_free(&cam_pkt);
    av_frame_free(&yuv_frame);
    sws_freeContext(yuyv2bgr);
    sws_freeContext(bgr2yuv);
    close_rtsp_output(&out_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_close_input(&in_ctx);
    return 0;
}
