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
