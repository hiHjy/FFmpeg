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
    //打开的这个输入整体 -------> ffmpeg -i input.mp4
    AVFormatContext *fmt_ctx = NULL;
    
    int ret = avformat_open_input(&fmt_ctx, "test.mp4", NULL, NULL);
    if (ret < 0) {
        print_error("avformat_open_input", ret);
        return ret;
    }

    // 解析流 ffmpeg -i input.mp4 输出的stream的相关行
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        print_error("avformat_find_stream_info", ret);
        return ret;
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
        // if (acp->codec_type == AVMEDIA_TYPE_VIDEO) {
        //     printf("视频流: stream#%u %u * %u\n", i, acp->width, acp->height);

        // }

        

    }


    avformat_close_input(&fmt_ctx); //释放资源

   return 0; 
}