#include <iostream>
#include <opencv2/opencv.hpp>
extern "C" {
    #include <stdio.h>
    #include <stdlib.h>
    #include <unistd.h>
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/error.h>
    #include <libswscale/swscale.h>
    #include <libavdevice/avdevice.h>
    #include <libavutil/pixdesc.h>
}
static void print_error(const char *msg, int err)
{
    char buf[256];
    av_strerror(err, buf, sizeof(buf));
    std::cerr << msg << ": " << buf << std::endl;
}

int main (int argc, char **argv) 
{
    AVFormatContext *fmt_ctx =nullptr;
    int ret = -1;
    cv::Mat cv_buf;
    avdevice_register_all();
    AVDictionary *opt = nullptr;
    av_dict_set(&opt, "framerate", "30", 0);
    av_dict_set(&opt, "video_size", "640x480", 0);
    av_dict_set(&opt, "pixel_format", "yuyv422", 0);

    if ((ret = avformat_open_input(&fmt_ctx, "0", av_find_input_format("avfoundation"),  &opt)) < 0) {
        print_error("avformat_open_input error", ret);
        return -1;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        print_error("avformat_find_stream_info error", ret);
        return -1;
    }

    int video_index = -1;
    for (int i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            printf("找的视频流编号%d\n", i);
            break;
        }
    }

    if (video_index < 0) {
        std::cout << "未找到视频流" << std::endl;
        return -1;
    }

    AVStream *vs = fmt_ctx->streams[video_index];
    AVCodecParameters *acp = vs->codecpar;
    SwsContext *sws_ctx = sws_getContext(
        acp->width,
        acp->height,
        (AVPixelFormat)acp->format,
        acp->width,
        acp->height,
        AV_PIX_FMT_BGR24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
        
    );
    if (!sws_ctx) {
        std::cout << "sws_ctx error" << std::endl;
        return -1;
    }
    cv_buf.create(acp->height, acp->width, CV_8UC3);
    uint8_t *cv_data[4] = {cv_buf.data, nullptr, nullptr, nullptr};
    int cv_linesize[4] = {(int)cv_buf.step[0], 0, 0, 0};
    
    std::cout << "fmt:" << av_pix_fmt_desc_get((AVPixelFormat)acp->format)->name << std::endl;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt||!frame) {
        std::cout << "av_packet_alloc or av_frame_alloc error" << std::endl;
        return -1;
    }

    int frame_count = 0;
    while (frame_count < 100) {
        ret = av_read_frame(fmt_ctx, pkt);
       
     

        if (ret == AVERROR(EAGAIN)) {
            usleep(1000);
            continue;
        }

        if (ret < 0) {
            print_error("av_read_frame", ret);
            return ret;
        }

        // //获取到了摄像头的一帧，格式是yuyv422，无需解码，只要封装成AVFrame即可
        // if (pkt->stream_index == video_index) {
        //     frame_count++;
        //     std::cout << "获取到第" << frame_count 
        //     << "帧---" << pkt->size << std::endl;
        // }

        frame->format = acp->format;
        frame->width = acp->width;
        frame->height = acp->height;

        frame->data[0] = pkt->data;
        frame->linesize[0] = acp->width *2;
        
        ret = sws_scale(
         sws_ctx,
         frame->data,
         frame->linesize,
         0,
         frame->height,
         cv_data,
         cv_linesize 
        );
        if (ret <= 0) {
            std::cerr << "sws_scale error" << std::endl;
            return -1;
        }   
        
        cv::imshow("cam", cv_buf);
        cv::waitKey(1);
       
        av_packet_unref(pkt);

    }



    return 0;
}
