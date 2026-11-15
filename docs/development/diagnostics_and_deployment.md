# 本地诊断与桌面部署

项目当前固定 Qt / Qt MQTT 6.12.0，最新双平台构建与部署范围见
[迁移记录](../validation/qt612.md)。下文原有 Qt 6.9 验证保留其历史适用范围。

## 运行日志

客户端在创建 GUI 之前建立本次启动的独立日志会话，业务工作线程和 Qt 对象释放后再写正常退出标记。
欢迎页“诊断”和 Esc“运行诊断”中的“打开本地日志目录”可以直接打开日志根目录。
创建目录或写盘失败会在该入口显示原因，视频、通信和界面仍可继续运行。

根目录由 Qt `QStandardPaths::AppLocalDataLocation` 决定，不依赖安装目录或当前工作目录。
应用身份保持 `organizationName=PIP`、`applicationName=PIPClient`，通常位置如下：

| 系统 | 默认日志根目录 |
| --- | --- |
| Windows | `%LOCALAPPDATA%/PIP/PIPClient/logs` |
| Ubuntu | `$XDG_DATA_HOME/PIP/PIPClient/logs`；未设置时为 `~/.local/share/PIP/PIPClient/logs` |

每次启动生成 `session_时间_PID_随机后缀` 子目录。时间使用 UTC，目录之间不会混写。

| 文件 | 用途 |
| --- | --- |
| `events_0.jsonl` | 当前日志，每行一个 JSON 对象；轮转后 `_1`、`_2`、`_3` 依次更早 |
| `session.json` | 会话状态、更新时间与正常结束时的退出码 |
| `active.lock` | 保留策略识别仍在运行的会话，不能用其存在与否判断是否崩溃 |
| `crash.txt` | 原生致命异常的紧急记录；正常退出删除空占位文件 |
| `crash.dmp` | Windows 尽力生成的最小转储；正常退出删除空占位文件 |

事件包含 UTC 时间 `utc`、本次进程启动后的单调时间 `uptimeUs`（微秒）、线程标识、
级别、类别、消息与结构化字段。跨设备对齐使用 UTC；分析同一进程的先后顺序和间隔使用
`uptimeUs`，避免系统校时影响时长。旧文件编号越大越早，分析轮转日志时应从最大编号读到 `_0`。

记录范围包括应用版本与构建身份、系统与 Qt 版本、Qt/QML 消息、导航与显示偏好变化、
MQTT 连接状态、比赛会话配置、UDP 视频状态、解码设备与错误。
界面线程每秒记录一次 `runtime_snapshot`，包含窗口、图形后端、视频接收/解码/呈交统计与 MQTT 计数。
这些是低频诊断快照，无法还原每个数据包或每帧视频。
FFmpeg warning/error 经过限流转交日志，避免坏码流造成无限输出。

视频呈现快照将解码后的等待拆为 `output.timings.mailboxWait`、`guiPrepare`、
`renderWait`，并保留总项 `guiWait`；`output.presentationFps` 为新视频帧 Qt 呈交率。
`gui.heartbeat` 是独立 20ms GUI 心跳的超时统计，可在无新画面时判断 GUI 调度是否停顿。
队列清空、解码器重置与输出超时由 `video/recovery_events` 按秒批量记录；
每批最多保留 32 条未消费事件，覆盖数量在 `omitted` 中显式记录，停止时冲刷尾部。
每条日志最多 4 个恢复事件；清队列前固定保留在途操作年龄、最近解码分段耗时、
最老 AU 等待年龄及最近 8 次入队节奏。接收批号描述用户态读包循环，不代表网卡到包批次。
字段的时间基准、代数、帧编号及触发原因见[图传诊断](../modules/video_receiver.md#解码恢复事件)。

磁盘写入由专属线程处理，生产者只将有界记录放入最多 1024 项的队列。
队列满时丢弃普通记录，并在后续成功写入的事件中统计 `droppedRecords`；超大字段会明确省略。
每文件默认 8 MiB，每会话最多 4 份事件文件，启动时保留最近 10 个会话。
事件文件的常规预算约 320 MiB；仍在运行的会话不会被清理，元数据和崩溃转储另计，
因此这不是整个目录的绝对总容量上限。

## 崩溃后的收集与分析

正常退出时 `session.json` 写为 `finished`，事件中包含 `shutdown_started` 和 `session_finished`。
没有完成正常退出流程的会话保留 `active`。
`active` 只能说明退出流程未完成，也可能来自强制结束、断电或日志写入失败，不能单独认定为程序崩溃。

Windows 在访问违规、`abort`、`terminate` 等路径尽力写入 `crash.txt` 和 `crash.dmp`。
Linux 对 SIGSEGV、SIGABRT、SIGILL、SIGFPE、SIGBUS 写入紧急记录，再恢复默认致命信号行为；
系统是否生成 core、保存在哪里以及如何收集，取决于该设备的 `ulimit`、`core_pattern`、
systemd-coredump 或其他系统崩溃服务配置。程序不会修改这些系统设置。

复盘时收集对应整个会话目录，并同时保留该版本的可执行文件、动态库与匹配调试符号。
版本号相同不足以证明二进制相同；应一并核对日志中的构建 revision、构建类型和 Qt/FFmpeg 依赖。
Release 发布应在构建归档中另存匹配符号，不能事后用另一轮编译的符号替代。

普通日志不采集视频帧、MQTT/UDP 原始负载或凭证。结构化敏感字段与常见文本形式会脱敏，
调用方仍必须避免将原始敏感内容交给日志接口。
IP、设备名称及系统信息属于诊断上下文；转储和 core 还可能包含进程内存中的数据，
因此分享之前需要检查收集内容。软件只在本机保存，不自动上传。

异步队列尚未落盘的尾部记录可能在崩溃时丢失；文件 flush 不等价于断电持久性。
OOM、栈耗尽、fast-fail、强杀、驱动或操作系统故障下，不能保证产生转储或完整文本证据。
日志可以帮助缩小故障范围，不能保证仅凭日志定位任意故障。

## 图标与应用身份

默认图标为用户选定的白灰橙“接收画幅”，正式资源位于 `resources/app/variants/receiver`。
设计过程见[接收画幅精修](../design/icons/receiver/readme.md)。
此前的 `viewlink`、`circuit`、`signal` 仅保留为历史资源，可显式选择以便回看。
已有 CMake 缓存不会自动改变选项；在已经配置依赖的构建目录中显式切换，
即可统一更新可执行文件、窗口与 Linux 桌面图标：

```sh
cmake -S . -B build -DPIP_APP_ICON_VARIANT=receiver
cmake --build build
```

可选值为 `receiver`、`viewlink`、`circuit`、`signal`，非法值会在配置阶段失败。
正式图标包含标准 SVG、16px 专用 SVG、16/24/32/48/64/128/256/512 像素 PNG，
以及使用相同 PNG 的 Windows 多尺寸 ICO。
普通构建直接使用已保存资源，不要求安装 Python；修改正式 SVG 后使用独立 Qt SVG 工具
与 `tools/generate_app_icons.py` 重新生成，对应命令和依赖见
[图标资源说明](../../resources/app/readme.md)。

Windows 的图标与版本资源嵌入 PE，可执行文件版本来自 CMake `PROJECT_VERSION`。
Linux 安装 `org.pip.PIPClient.desktop` 和同名 hicolor 图标，桌面 ID 为 `org.pip.PIPClient`。
`StartupWMClass=PIPClient` 对应 Qt 的既有应用名；Wayland 通过 desktop ID 关联应用。
桌面关联规则与文件正确，不等于已经验证所有桌面环境、图标缓存和 DPI 组合。

## 安装与打包

先在本机选择兼容的 Qt、Qt MQTT、Protobuf 与 FFmpeg SDK，完成配置、构建和测试，
再安装到单独暂存目录。下面命令假定 `build` 已正确配置；多配置生成器需另指定一致的 `--config`。

```sh
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix staging
cpack --config build/CPackConfig.cmake
```

Windows 默认生成 ZIP，Linux 默认生成 TGZ。压缩包是部署产物，不会注册卸载程序或修改系统设置。
安装前缀由调用方选择；验证时使用独立暂存目录，不应直接覆盖正在运行的客户端。

Qt 运行库和 QML 插件通过所选 Qt kit 的部署工具收集。
Windows 的 Qt MQTT 可能安装在基础 kit 之外，而已验证的 Qt 6.12 `windeployqt` 使用单一 Qt DLL 查找目录。
部署规则只在构建目录建立私有 Qt 视图并放入所选 MQTT DLL，交给部署工具解析；
它不会改写系统 Qt kit，也不通过忽略依赖错误掩盖缺失库。
私有视图中的开发机路径仅用于安装过程，不进入客户端的相对路径 `qt.conf`。

Windows 的 FFmpeg 运行库按实际 PE 导入关系收集，包含当前构建所需的传递依赖，
而不是固定复制三个 DLL。先检查 `PIP_FFMPEG_RUNTIME_DIR`，未显式指定时使用 SDK 的 `bin`；
头文件/导入库 SDK 没有 `bin` 时，兼容既有配置并明确警告使用所选 Qt kit 的运行库。
非系统依赖必须来自同一运行库目录，缺失、PE 架构不符或导入其他 FFmpeg 主版本时配置失败。
该检查不替代目标设备上的驱动、系统库与编译器运行时验证；本轮实测为 Windows x64 / MinGW。

发布前仍需在无开发环境的目标设备检查依赖、平台插件、QML 插件和视频解码器。
Ubuntu 部署依赖所选 Qt 的公开 Linux 部署能力，系统图形驱动和系统基础库需要符合目标发行版基线。
安装时另从实际 QtCore 解析非系统依赖，搜索所选库目录及其父目录，
补齐官方 SDK 根目录中的 ICU 等库并保留 SONAME 链；未解析依赖会终止安装。
该补充不写死 ICU 版本，不复制系统基础库，也不替代目标设备验收。
Intel/NVIDIA 视频能力与驱动兼容性仍按[平台约束](../platform_compatibility.md)单独验收。

FFmpeg 的 LGPL 文本与所用二进制说明安装到 `share/PIPClient/licenses`。
Qt、Qt MQTT、Protobuf、字体、图标及其他实际随包依赖的许可证、源代码提供和通知义务，
应按最终选用的版本与构建方式逐项检查；自动复制运行库并不代表完成所有发布许可要求。

## 初轮 Qt 6.9 验证范围

本轮日志与原生崩溃组件已在 Windows 和 Ubuntu 构建运行，崩溃验证使用独立子进程，
覆盖主线程/工作线程、访问违规或致命信号、`abort`、`terminate` 与真实 Qt fatal 链路。
这验证了组件行为，不能替代完整客户端在目标系统上的部署验收。

Windows 完整应用的构建、15 组 CTest 与 QML 静态检查通过，包含 FFmpeg 传递依赖拒绝回归。
Ubuntu 22.04 / Qt 6.9 的日志测试 10 项、崩溃测试 13 项通过，无跳过。
三套图标的 Ubuntu 资源小工程均完成编译、qrc 加载与 desktop 校验，9 个安装图像与源文件一致。
Windows 暂存目录在去除开发环境 PATH 后完成原生 Direct3D 11 启动，8 秒内保存 7 次快照，
正常退出码为 0，会话标记 finished，空崩溃占位文件已清理，未记录 warning/error。
该短测验证启动与日志生命周期，不代表视频或硬件长稳验收。
Windows ZIP 已通过 CPack 生成；三套 PE 图标的七档图层与源 ICO 逐字节一致。
Ubuntu 主程序的 Qt MQTT/FFmpeg/QtCore 外部依赖部署已检查，原生 X11/Wayland 桌面启动与 GPU 长期运行尚未完成验收。
离屏组件测试不能证明原生桌面窗口、图标显示或真实图传硬解兼容性。

### 选定图标初轮接入验证（2026-11-13，Qt 6.9）

- Windows Qt 6.9.0 / MinGW 13.1 完整客户端构建及 15 组 CTest 通过；
  当前 CLion 构建已显式切换为 `receiver`。客户端和资源探针中的七档 PE 图标负载
  与选定白灰橙 ICO 逐字节一致，探针的八档 qrc PNG 与 QIcon 检查通过。
- Ubuntu 22.04.5 / GCC 11.4 / Qt 6.9 资源工程构建、安装前后离屏加载通过；
  八档 qrc PNG 与正式资源逐像素一致，desktop ID 与透明通道检查通过。
  `desktop-file-validate` 通过，安装的八档 PNG 和 scalable SVG 共九个文件与源文件逐字节一致。
- Windows 独立生成工具重现八档正式 PNG 像素与 ICO 字节，旧 `circuit` 输出保持一致；
  缺参数、非法 SVG、输出目录失败路径返回明确错误。
- 初轮独立生成工具未在 Ubuntu 构建，原因是当时 CMake/Qt SVG 开发文件不足；
  后续已用 Qt 6.12 在两平台构建运行并生成八档 PNG。普通产品不依赖该工具，详见迁移记录。

本次未重新生成部署压缩包；已有压缩包保留之前的图标，发布前需从当前源码重新打包。
上述验证不包含 Windows 外壳图标缓存/DPI 实际显示、Ubuntu 原生 X11/Wayland 桌面显示，
也不替代 Ubuntu 完整客户端的部署验收。
