#pragma once

#include <QString>
#include <vector>

/** 平台设备描述值，不持原生句柄；key 为持久化身份，device 为当前驱动枚举编号/路径。 */
struct VideoDecodeDevice {
    QString key;      ///< 后端 + 设备身份；不以可变化的 GPU 排序作为已保存选择。
    QString backend;  ///< FFmpeg 的 d3d11va / vaapi / cuda。
    QString device;   ///< 只传给对应 FFmpeg 后端；不进入网络协议。
    QString label;    ///< 硬件名及后端；重复后端允许对应同一块物理 GPU。
};

/** 枚举平台图形设备及可选 CUDA driver，不创建视频解码上下文；供工作/探测线程使用。
 * Windows 用 DXGI LUID（重启/驱动变更后可能失效），Linux 用 DRM sysfs 身份，CUDA 用 UUID。
 * 不要求 CUDA SDK；旧身份失效时回退 CPU，不能静默匹配另一张 GPU。
 * 枚举成功不代表设备支持当前 HEVC profile，需另外初始化和实际码流验证。
 */
std::vector<VideoDecodeDevice> videoDecodeDevices();

/** 获取 CPU 名称，读取失败时返回架构名；不探测 CPU 视频指令集性能。 */
QString videoCpuName();
