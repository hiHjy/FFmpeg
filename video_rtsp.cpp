
/**
 * 本文件的作用是从摄像头读100帧数据，
 * 交由opencv处理后重新编码为h264，然后写到一个rtsp文件里。
 * 2026-1-25 我的生日欧
 * 
 * 
 * 
 * 
 * 
 */

/*          笔记
架构
H.264 packet
 → rtsp mux
 → 文件

 理解mux:
     mux = 媒体数据的秩序层
     mux = 时间 + 参数 + 结构
     mux = 推流不可缺的中间层
     编码解决“怎么压缩”，
    mux 解决“别人怎么理解你压缩出来的东西”。
*/

// 1. 打开摄像头 (avfoundation)
// 2. 创建 RTSP 输出 + 新 stream
// 3. 初始化 x264 编码器
// 4. 写 RTSP header（建立会话）
// 5. 采集一帧摄像头数据
// 6. YUYV → BGR (给 OpenCV)
// 7. OpenCV 画框
// 8. BGR → YUV420P
// 9. 送编码器，取 packet
// 10. 写入 RTSP

#include <iostream>
#include <opencv2/opencv.hpp>
extern "C" {
    #include <stdio.h>
    #include <stdlib.h>
    #include <unistd.h>
    #include <fcntl.h>
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
    avformat_network_init();
    AVFormatContext *fmt_ctx =nullptr;
    int ret = -1;
    cv::Mat cv_buf;
    avdevice_register_all();
    AVDictionary *opt = nullptr;
    av_dict_set(&opt, "framerate", "30", 0);
    av_dict_set(&opt, "video_size", "640x480", 0);
    av_dict_set(&opt, "pixel_format", "yuyv422", 0);
    
    //** 创建rtsp输出上下文 */
    AVFormatContext *ofmt = nullptr;
    avformat_alloc_output_context2(&ofmt ,nullptr, "rtsp", "rtsp://127.0.0.1:8554/live");
    if (!ofmt) {
        std::cerr << "avformat_alloc_output_context2 failed\n";
        return -1;
    }
    
    //给这个rtsp格式的文件创建一个新流
    AVStream *new_stream = avformat_new_stream(ofmt, nullptr);
    if (!new_stream) {
        std::cerr << "avformat_new_stream failed\n";
        return -1;
    }

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

    /* -------------------------创建编码器--------------------- */

    const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) {
        std::cerr << " avcodec_find_encoder_by_name(\"libx264\") error" << std::endl;
        return -1;
    }

    AVCodecContext *enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        std::cerr << "avcodec_alloc_context3 error" << std::endl;
        return -1;
    }
    enc_ctx->width = acp->width;
    enc_ctx->height = acp->height;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P; //编码器得是这个格式
    enc_ctx->time_base = {1, 30};
    enc_ctx->framerate = {30, 1};
    enc_ctx->bit_rate = 800000; //不知道是啥
    enc_ctx->max_b_frames = 0; //关闭B帧
    enc_ctx->gop_size = 30; //每 30 帧来一个“完整可独立解码的 I 帧（关键帧)
    enc_ctx->keyint_min = 30; //保证至少每30帧就会有一个关键帧
    av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(enc_ctx->priv_data, "preset", "veryfast", 0);


  
   
    
    if (ofmt->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }


    ret = avcodec_open2(enc_ctx, encoder, NULL); //启动
    if (ret < 0) {
        print_error("avcodec_open2", ret);
        return -1;
    }
   //****** 先 open 编码器，再拷贝参数到 stream ********//
    //把编码器的信息拷贝到流中
    ret = avcodec_parameters_from_context(new_stream->codecpar, enc_ctx);
    
    if (ret < 0) {
        print_error("avcodec_parameters_from_context", ret);
        return -1;
    }
    new_stream->time_base = enc_ctx->time_base;



    AVDictionary *rtsp_opts = nullptr;
    //av_dict_set(&rtsp_opts, "rtsp_flags", "listen", 0);
    av_dict_set(&rtsp_opts, "rtsp_transport", "tcp", 0);
    //将容器rtsp的头部信息（Header） 写入文件。
    ret = avformat_write_header(ofmt, &rtsp_opts);
    if (ret < 0) {
        print_error("avformat_write_header", ret);
        return -1;
    }

    std::cerr << "enc time_base = " << enc_ctx->time_base.num << "/" << enc_ctx->time_base.den << "\n";
    std::cerr << "enc framerate = " << enc_ctx->framerate.num << "/" << enc_ctx->framerate.den << "\n";


    //编码要用的buf
    AVFrame *enc_frame = av_frame_alloc();
    if (!enc_frame) {
        std::cerr << "av_frame_alloc() error" << std::endl;
        return -1;
    }

    enc_frame->width = enc_ctx->width;
    enc_frame->height = enc_ctx->height;
    enc_frame->format = enc_ctx->pix_fmt;

    ret = av_frame_get_buffer(enc_frame, 32);//按格式给你分配YUV三个平面，32位内存对齐
    if (ret < 0) {
        print_error("av_frame_get_buffer error", ret);
        return -1;
    }


    /*---------------------------------------------------------*/
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

    SwsContext *sws_enc_ctx =  sws_getContext(
        acp->width,
        acp->height,
        AV_PIX_FMT_BGR24,
        acp->width,
        acp->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
        
    );
    if (!sws_ctx || !sws_enc_ctx) {
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
    int enc_count = 0;
    int frame_count = 0;
    int pts = 0;
    AVPacket *enc_pkt = av_packet_alloc();
    while (true) {
        ret = av_read_frame(fmt_ctx, pkt);
       
        

        if (ret == AVERROR(EAGAIN)) {
            usleep(1000);
            continue;
        }

        if (ret < 0) {
            print_error("av_read_frame", ret);
            return ret;
        }
        if (pkt->stream_index != video_index) {
            av_packet_unref(pkt);
            continue;
        }

        //获取到了摄像头的一帧，格式是yuyv422，无需解码，只要封装成AVFrame即可
        if (pkt->stream_index == video_index) {
            frame_count++;
            std::cout << "获取到第" << frame_count 
            << "帧---" << pkt->size << std::endl;
            
        }

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


        /****** 模拟cv处理后，在编码 *******************/

        //cv::Mat gray;
        //cv::cvtColor(cv_buf, gray, cv::COLOR_BGR2GRAY);
        // cv::imshow("cam", gray);
        cv::rectangle(cv_buf, cv::Rect(100, 100, 200, 150), cv::Scalar(0, 255, 0));
        // cv::imshow("origin", cv_buf);
        // cv::waitKey(1);
        
        ret = av_frame_make_writable(enc_frame); //防止上一帧没处理完，又开始转换下一帧，导致数据被覆盖
        if (ret < 0) {
            print_error("av_frame_make_writable", ret);
            return -1;
        }
        ret = sws_scale( //cv处理后的数据，转为yuv420p
            sws_enc_ctx,
            cv_data,
            cv_linesize,
            0,
            acp->height,
            enc_frame->data,
            enc_frame->linesize
        );

        //编码
        if (ret <= 0) {
            print_error("sws_scale error[enc]", ret); 
            return -1;
        }
        enc_frame->pts = pts++;
        enc_frame->duration = 1;
        ret = avcodec_send_frame(enc_ctx, enc_frame);
        if (ret < 0) {
            print_error("sws_scale error[enc]", ret); 
            return -1;
        }
        
        //编码开始
        while (1) {
            ret = avcodec_receive_packet(enc_ctx, enc_pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                print_error("avcodec_receive_packet error", ret);
                return -1;
            }
            enc_pkt->duration =1;
            
            //打包了一个packet
            // enc_count++;
            // std::cout << "编码获得第 "<< enc_count << "个packet" << std::endl;
            printf("pts=%d, time=%.3f\n",(int)enc_frame->pts,enc_frame->pts * av_q2d(enc_ctx->time_base));

            enc_pkt->stream_index = new_stream->index;

            //时间戳换算
            av_packet_rescale_ts(
                enc_pkt,
                enc_ctx->time_base,
                new_stream->time_base
            );
            
            ret = av_interleaved_write_frame(ofmt, enc_pkt); //推流
            if (ret < 0) {
                print_error("av_interleaved_write_frame", ret);
                return -1;
            }
            
            av_packet_unref(enc_pkt);
        }



        av_packet_unref(pkt);

    }
    //flush
    avcodec_send_frame(enc_ctx, NULL);
    while (1) {
        ret = avcodec_receive_packet(enc_ctx, enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) { print_error("flush receive_packet", ret); return -1; }
        

        std::cout << "[flush]" << std::endl;
        enc_pkt->stream_index = new_stream->index;      
        //时间戳换算
        av_packet_rescale_ts(
            enc_pkt,
            enc_ctx->time_base,
            new_stream->time_base
        );

        ret = av_interleaved_write_frame(ofmt, enc_pkt);
        if (ret < 0) {
            print_error("av_interleaved_write_frame", ret);
            return -1;
        }

        av_packet_unref(enc_pkt);

    }

    av_write_trailer(ofmt);
    avformat_free_context(ofmt);
    std::cout << "编码完成" << std::endl;
    


    return 0;
}
