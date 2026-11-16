# HEVC 回归样本

`video.hevc` 是 FFmpeg `testsrc2` 生成的 320×180 单帧测试图案（1 fps、8 bit YUV420、SAR 1:1），
通过 libx265 编为 Annex B HEVC IDR，包含 VPS/SPS/PPS。无第三方赛事图片或个人数据。

生成命令（测试运行不需要 ffmpeg CLI 或编码器）：

```sh
ffmpeg -f lavfi -i testsrc2=size=320x180:rate=1 -frames:v 1 \
    -c:v libx265 -preset ultrafast -tune zerolatency \
    -x265-params keyint=1:bframes=0:repeat-headers=1:log-level=error -f hevc video.hevc
```

本地生成工具为 FFmpeg 2025-08-16 开发构建（libavcodec 62），测试解码使用项目 FFmpeg 7
（libavcodec 61）。样本用于验证标准 HEVC 互通，不验证编码器性能或官方设备互通。

`video_hd.hevc` 是同一工具生成的 1920×1080 SMPTE HD 彩条单个 IDR，60 fps 标记、
8 bit YUV420、SAR 1:1、Annex B，含 AUD/VPS/SPS/PPS；用于持续接收时重复保存、停止、
重开的资源回收回归。测试以 16 ms 周期发送此帧，不代表真实相机采集或完整帧间编码性能。
生成参数为 `-f lavfi -i smptehdbars=size=1920x1080:rate=60 -frames:v 1 -c:v libx265
-preset ultrafast -tune zerolatency -x265-params
aud=1:repeat-headers=1:keyint=60:min-keyint=60:scenecut=0:bframes=0:pools=2:frame-threads=1:log-level=error
-f hevc video_hd.hevc`。与小样本一样无赛事或个人数据。

`video_hd10.hevc` 为同样的自产 1920×1080 彩条，增加 `-pix_fmt yuv420p10le
-color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv`，其余参数同上。
它使用 HEVC Main 10，用于真实硬件 P010 平面导入、CPU 下载回退及截图颜色对照；
生成不属于测试运行依赖，不据此宣称 HDR 显示能力。
