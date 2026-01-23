# ffmpeg
## 重要命令

1. 显示文件详细信息

   ```bash
   ffmpeg -i input.mp4
   ```

   执行命令后会得到如下关键的东西

   ```bash
   	Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'input.mp4':
    	Duration: 00:00:10.04, start: 0.000000, bitrate: 2500 kb/s
   
     Stream #0:0: Video: h264 (High), yuv420p, 1920x1080, 25 fps
     Stream #0:1: Audio: aac, 44100 Hz, stereo, fltp
   ```

   >Input #0  	我可以有多个输入文件 那我给它们编号：#0、#1、#2 …
   >
   >
   >
   >stream #0:0: Video: h264, yuv420p, 1920x1080, 25 fps 
   >
   >​	Stream #0:0
   >
   >​		含义是：
   >
   >​			第一个 `0`：来自 Input #0
   >
   >​			第二个 `0`：这是这个输入里的 **第 0 条流**
   >
   >
   >
   >​	Video: h264, yuv420p 		h264 解码后 → yuv420p
   >
   >
   >
   >Audio: aac, 44100 Hz, stereo, fltp
   >
   >​	aac：压缩格式
   >
   >​	fltp：**解码后的音频格式**
   >
   >​	
   >
   >

2. 获得视频流

   ```bash
   ffmpeg -i input.mp4 -an -c:v copy out.h264
   ```

   > 不解码，只拿数据流
   >
   > -an = disable audio 不要音频流
   >
   > -c:v copy 视频流使用 copy 模式，不走编码器

3. 播放视频流

   ```bash
   ffplay -f h264 out.h264
   ```

   > ```
   > ffplay -f h264 out.h264
   > │       │       └── 输入文件名
   > │       └── 强制指定格式为 H.264
   > └── FFmpeg 的播放器组件
   > ```

4. 解码为yuv

   ```bash
   ffmpeg -i input.mp4 -vf fps=1 frame_%03d.yuv
   ```

   > 从视频里
   >  **每秒取 1 帧**
   >  **解码成原始像素数据**
   >  保存成 `.yuv` 文件

5. 播放yuv

   ```bash
   ffplay -f rawvideo -pixel_format yuv420p -video_size 1280x720 f.yuv 
   ```

   > 没有容器
   >
   > 没有时间戳
   >
   > 没有元数据
   >
   > 
   >
   > 这一条命令，背后等价于：
   >
   > 1. 打开输入（format）
   > 2. 找 video stream
   > 3. 创建 decoder
   > 4. packet → frame
   > 5. frame 里是：
   >    - `frame->data[0]` → Y
   >    - `frame->data[1]` → U
   >    - `frame->data[2]` → V

6. yuv->rgb

   ```bash
   ffmpeg -i 2.mp4 -vf fps=1,format=rgb24 -f rawvideo f.rgb
   ```

7. 播放rgb

   ```bash
   ffplay -f rawvideo -pixel_format rgb24 -video_size 1280x720 f.rgb
   ```

8. 逐帧输出原始rgb数据到标准输出然后用ffplay去解析并输出到标准输出

   ```bash
   ffmpeg -i 2.mp4 -f rawvideo -pix_fmt rgb24 - | \
   ffplay -f rawvideo -pixel_format rgb24 -video_size 1280x720 -
   ```

   > -： 标准输出

9. 调用摄像头（mac)

   ``` bash
   ffplay -f avfoundation -framerate 30 -video_size 640x480 -i "0"
   ```


# 库的使用
## 一、整体认知（先立世界观）

### 1️⃣ 容器 vs 编码

- **容器（Container）**
  - mp4 / mkv / flv / rtsp
  - 作用：存放多路流（视频 / 音频 / 字幕）
- **编码（Codec）**
  - H.264 / H.265 / AAC
  - 作用：压缩数据

FFmpeg 的职责：

- `libavformat`：处理**容器**
- `libavcodec`：处理**编码 / 解码**

------

### 2️⃣ Packet 与 Frame 的本质区别

| 名称       | 含义                               |
| ---------- | ---------------------------------- |
| `AVPacket` | **压缩后的原始数据**（来自容器）   |
| `AVFrame`  | **解码后的一帧数据**（可直接使用） |

关键认知：

- **AVPacket ≠ 一帧**
- 一个 packet：
  - 可能是半帧
  - 可能是多帧
  - 可能只是 SPS / PPS
- **绝不能假设：packet == frame**

------

## 二、解码整体流程（你现在已经完整跑通）

### Step 1：打开输入（容器层）

```
AVFormatContext *fmt_ctx = NULL;
avformat_open_input(&fmt_ctx, "test.mp4", NULL, NULL);
avformat_find_stream_info(fmt_ctx, NULL);
```

含义：

- 打开 mp4 文件
- 解析容器结构，获取流信息

------

### Step 2：枚举流，找到视频流

```
for (i = 0; i < fmt_ctx->nb_streams; i++) {
    AVCodecParameters *acp = fmt_ctx->streams[i]->codecpar;
    if (acp->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_index = i;
    }
}
```

- 一个文件里可能有多路流
- 视频流、音频流是 **交错出现的**
- 必须记录 `video_index`

------

### Step 3：找到解码器并打开

```
decoder = avcodec_find_decoder(video_acp->codec_id);
dec_ctx = avcodec_alloc_context3(decoder);
avcodec_parameters_to_context(dec_ctx, video_acp);
avcodec_open2(dec_ctx, decoder, NULL);
```

你现在清楚地知道：

- `AVCodecParameters`：**说明书**
- `AVCodec`：**解码器型号**
- `AVCodecContext`：**真正工作的解码器实例**

------

## 三、send / receive 模型（核心理解）

### 1️⃣ send / receive 是**解耦的**

```
Packet（输入） → 解码器内部缓存 → Frame（输出）
```

- `avcodec_send_packet`：投喂压缩数据
- `avcodec_receive_frame`：尝试取一帧解码结果
- **二者不是一进一出**

------

### 2️⃣ 为什么 receive 必须循环？

```
avcodec_send_packet(dec_ctx, pkt);

while (1) {
    ret = avcodec_receive_frame(dec_ctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
    // got frame
}
```

原因：

- 一个 packet：
  - 可能解不出 frame
  - 也可能解出多个 frame
- `receive_frame` **一次最多返回一帧**
- 所以必须循环接收，直到：
  - `EAGAIN`（需要更多 packet）
  - `EOF`

------

### 3️⃣ EAGAIN 的真实语义（你已经吃透）

| 场景       | EAGAIN 含义     | 正确行为                     |
| ---------- | --------------- | ---------------------------- |
| 解码阶段   | 需要更多 packet | 回到外层继续 `av_read_frame` |
| flush 阶段 | 没东西可吐了    | `break` 退出                 |

------

## 四、主解码循环（标准模板）

```
while (av_read_frame(fmt_ctx, pkt) >= 0) {

    if (pkt->stream_index == video_index) {

        avcodec_send_packet(dec_ctx, pkt);

        while (1) {
            ret = avcodec_receive_frame(dec_ctx, frame);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;

            // 成功拿到一帧
            printf("get frame %dx%d\n", frame->width, frame->height);
            av_frame_unref(frame);
        }
    }

    av_packet_unref(pkt);
}
```

你现在已经理解：

- `av_read_frame` 读的是 **packet**
- packet 可能是视频 / 音频
- 只有 `stream_index == video_index` 才送解码器

------

## 五、为什么必须 flush（你已经亲眼验证）

### 1️⃣ 问题来源

- H.264 / H.265 存在：
  - B 帧
  - 帧重排
  - 延迟输出

**文件读完 ≠ 解码结束**

------

### 2️⃣ flush 的标准写法

```
avcodec_send_packet(dec_ctx, NULL); // 通知解码器：没输入了

while (1) {
    ret = avcodec_receive_frame(dec_ctx, frame);

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        break;

    printf("[flush] get frame\n");
    av_frame_unref(frame);
}
```

- flush 阶段还能拿到帧
- 不 flush 会丢最后几帧

------





## 音频

### 为什么要进行数模转换

外界声音是模拟信号，计算机需要二进制，所以，需要ADC

### PCM

PCM是音频的原始数据，要获得原始数据需要知道3个概念

- 采样大小：一个采样用多少bit来存放
- 采用的频率：8K 16K ...
- 声道数：单声道 双声道 多声道

码率：采样大小 x 频率 x 声道数 

码率表示一秒的大小，这个数据是非常大的，所以需要编码，使得它变小