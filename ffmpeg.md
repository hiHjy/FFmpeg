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

## 库的使用

- 

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