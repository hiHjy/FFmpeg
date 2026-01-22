#include <iostream>

extern "C" {

#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
}

using namespace std;
int main (int argc, char **argv) 
{
    //注册所有的设备
    avdevice_register_all(); 

    //获得格式
    const AVInputFormat * format = av_find_input_format("avfoundation");
    AVFormatContext *context = NULL; //上下文
    //打开设备

    char err[1024];
    printf("打开设备前\n");
    int ret = avformat_open_input(&context, ":1", format, NULL);
    if (ret != 0) {
        av_strerror(ret, err, sizeof(err));
        printf("%s\n", err);
        return -1;  
    }
    printf("打开设备后\n");

    printf("获取流信息前\n");
    ret = avformat_find_stream_info(context, NULL);
    assert(ret >= 0);
    printf("获取流信息后\n");
    printf("获取流信息后，流数量：%d\n", context->nb_streams);

    // 打印流信息（调试用）
    av_dump_format(context, 0, ":1", 0);

    printf("申请内存前\n");
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) { 
        printf("av_packet_alloc\n");
        return -1;
    }
     printf("申请内存前\n");
    //av_init_packet(&pkt);
    //从设备中读取音频
    printf("进入循环前\n");
    int count = 0;
    while (true) { 


        //printf("111%d\n", pkt.size);
        av_packet_unref(pkt);
        ret = av_read_frame(context, pkt);
        if (ret != 0) {
            av_strerror(ret, err, sizeof(err));
            if (ret == AVERROR(EAGAIN)) {

                printf("不是错误\n");
                usleep(500000);
                continue;
            }
            printf("%s\n", err);
            return -1;
            
        }
        printf("第%d个数据包,大小:%d\n", count, pkt->size);
        count++;
        if (count >= 10) {
            printf("已读满10个数据包\n");
            av_packet_unref(pkt);
            av_packet_free(&pkt);
            return 0;
        }
        // context->
        
        // printf("hhh\n");

    }

    return 0;
}