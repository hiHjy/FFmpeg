#include <iostream>

extern "C" {

#include <stdio.h>
#include <stdlib.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>

}
static void print_error(const char *msg, int err)
{
    char buf[256];
    av_strerror(err, buf, sizeof(buf));
    fprintf(stderr, "%s: %s\n", msg, buf);
    
}

/*
    result：
    nb_stream = 2
    stream #0: type:video codec_id=27       h.264
    stream #1: type:audio codec_id=86018    aac 
*/

using namespace std;
int main (int argc, char **argv) 
{
    int i;
    int video_index = -1;
    const AVCodec *decoder = NULL;
    AVCodecParameters *video_acp = NULL;
    const char *codec_name = NULL;
    AVCodecContext *dec_ctx = NULL;
     int n = 0;
    //打开的这个输入整体 -------> ffmpeg -i input.mp4
    AVFormatContext *fmt_ctx = NULL;
    AVPacket *pkt = NULL;   //原始数据
    AVFrame *frame;         //解码后的一帧数据
    avformat_network_init();
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0); // 5秒，单位微秒
    int ret = avformat_open_input(&fmt_ctx, "rtsp://192.168.1.30:8554/live", NULL, &opts);
    if (ret < 0) {
        print_error("avformat_open_input", ret);
        return ret;
    }
    std::cout << "rtsp连接成功" << std::endl;
        // 解析流 ffmpeg -i input.mp4 输出的stream的相关行
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        print_error("avformat_find_stream_info", ret);
        goto err;
    }

    //输出有几个流 video and audio
    printf("nb_stream = %d\n", fmt_ctx->nb_streams); 

    //遍历每个流
    for (i = 0; i < fmt_ctx->nb_streams; ++i) {
        //获取每个流的指针
        AVStream *stream = fmt_ctx->streams[i]; 

        //获取这个流的的参数
        AVCodecParameters *acp = stream->codecpar;
        const char *type;
        switch (acp->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            /* code */
            type = "video";
            break;
        case AVMEDIA_TYPE_AUDIO:
            type = "audio";
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            type = "subtitle";
        
        default:
            type = "other";
            break;
        }
        printf("stream #%u: type:%s codec_id=%d\n", i, type, acp->codec_id);
        if (acp->codec_type == AVMEDIA_TYPE_VIDEO) {
            //printf("视频流: stream#%u %u * %u\n", i, acp->width, acp->height);
            video_index = i; //拿视频流的索引
           
        }
       
        

    }
    if (video_index < 0) {
           fprintf(stderr, "no video stream\n");
           goto err;
    }

    video_acp = fmt_ctx->streams[video_index]->codecpar;  //拿到视频流的参数信息
    codec_name = avcodec_get_name(video_acp->codec_id);          //拿视频流的编码格式
    printf("video stream index:%d  codec_name:%s\n", video_index, codec_name);

    //获得解码器的型号
    decoder = avcodec_find_decoder(video_acp->codec_id);
    if (decoder == NULL) {
        fprintf(stderr, "no %s decoder\n", codec_name);
        goto err;
    }

    

    //创建解码器实例
    dec_ctx = avcodec_alloc_context3(decoder);
    if (dec_ctx == NULL) {
        fprintf(stderr, "avcodec_alloc_context3 error\n");
        goto err;
    }
    
    //将视频流的参数信息交给解码器
    ret = avcodec_parameters_to_context(dec_ctx, video_acp);
    if (ret < 0) {
        print_error("avcodec_parameters_to_context error", ret);
        goto err;
    }
    
    //开启解码器
    ret = avcodec_open2(dec_ctx, decoder, NULL);
    if (ret < 0) {
        print_error("avcodec_open2 error", ret);
        goto err;
    }
    
    //走到这里已经启动了视频解码器
    printf("decoder opened: %s\n", decoder->name);
    printf("decoded video: size=%d * %d pix_fmt=%d\n", dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt);
   

    /* ----- 开始解码 ------*/
    pkt = av_packet_alloc();
    frame = av_frame_alloc();

    if (!pkt || !frame) {
        fprintf(stderr, "av_packet_alloc or av_frame_alloc error\n");
        goto err;
    }

    printf("start decoding...\n");
/*

    Packet 是“压缩后的原材料”，Frame 是“解码后的成品”。
    send / receive 不是一进一出，而是“解耦的生产线”。
    AVPacket ≠ 一帧
    AVPacket 是：从容器（mp4/mkv/rtsp）里读出来的一段压缩码流
    对于 H.264 / H.265 来说：
                        一个 packet：
                                可能是半帧
                                可能是多帧
                                可能只是 SPS / PPS
                                绝对不能假设：packet == frame




*/
   
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        printf("packet stream_index:%d(video index:%d)\n", pkt->stream_index, video_index);
        //只处理视频流
       if (pkt->stream_index == video_index) {
            //pkt 是原始的h264数据，将这个原始数据拿去给dec_ctx去解码
            ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret != 0) {
                print_error("avcodec_send_packet", ret);
                goto err;
            }
            
            while (ret >= 0) {
                //“现在有没有一帧解码完成的数据？” 是 ret = 0
                ret = avcodec_receive_frame(dec_ctx, frame);
                //解码阶段EAGAIN 需要更多 packet
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    
                    break;
                }
                
                
                if (ret < 0) {
                    print_error("avcodec_receive_frame", ret);
                    goto err;
                }

                //程序到这里成功获取了一帧yuv420数据
                printf("get frame:%dx%d pix_fmt=%d n=%d\n", frame->width, frame->height, frame->format, ++n);
                //return ret;
                av_frame_unref(frame);
            }
            
       } 
       av_packet_unref(pkt); //表示购物车已经清空了，可以继续进货了
       
    }

    //视频文件读完了，while (av_read_frame(fmt_ctx, pkt) >= 0) 会退出
    //此时应该flush，让解码器内部缓存的帧读出来
    ret = avcodec_send_packet(dec_ctx, NULL); //flush
    if (ret < 0) {
        print_error("flush error", ret);
        goto err;
    }

    while (1) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR_EOF) {
            printf("flush done，decoded successfully\n");
            break;
        }
        //flush阶段EAGAIN :需要更多输入才会有帧 退出
        if (ret == AVERROR(EAGAIN)) {
            printf("flush EAGAIN\n");
            break;
        }

        if (ret < 0) {
            printf("flush error:receive_frame");
            goto err;
        }
        printf("[flush]get frame:%dx%d pix_fmt=%d n=%d\n", frame->width, frame->height, frame->format, ++n);
        av_frame_unref(frame);
    }




    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    return 0;
   
err:
    if (dec_ctx) {
        avcodec_free_context(&dec_ctx);
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }
    if (pkt) {
        av_packet_free(&pkt);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    avformat_network_deinit();
    return ret;
}