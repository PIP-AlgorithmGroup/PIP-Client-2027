# 目录整理验收

## 范围

2026-11-14 在 `main` 整理项目目录。通信源码归入 `src/communication`，
日志与运行诊断归入 `src/diagnostics`；QML 按共享控件、欢迎页、设置、视频分区，
测试按被测模块分区，共用夹具放入 `tests/support`。
根工程通过 `add_subdirectory(tests)` 管理测试，Linux 安装资源归入 `packaging/linux`。
文档按协议、模块、开发、设计、研究和验收归类，并补充[索引](../README.md)与
[目录约定](../directory_layout.md)。未引入新业务模块或依赖。

本轮核对 244 个迁移后的有效文件，未发现丢失或意外删除。
图片、字体、图标、协议 PDF 等资源内容保持一致；文本差异仅涉及路径、导入、
构建组织、格式和说明。删除的 `.gitkeep` 不包含业务内容。
运行时资源仍使用原有 `qrc:/resources/...`，协议、配置存储和用户日志位置不迁移。

## 本地构建与 IDE

- Windows 日常构建：`build/windows/qt612-debug`。
- Ubuntu 日常构建：`build/linux/qt612-debug`。
- 安装验收暂存：`build/staging/windows`、`build/staging/linux`。
- 本地依赖：`third_party/sources` 与 `third_party/installed/<系统>/<依赖-版本>`；
  外部 SDK 仍可通过显式 CMake 前缀指定。
- 历史根目录构建：`build/archive`；历史日志和临时探针：`.cache/archive`。
  保存了绝对路径的旧 CMake 构建树仅作归档，不能直接在新位置继续构建。

CLion 保存的主配置已指向新的 Windows 构建目录与依赖前缀。
当前运行中的 IDE 仍可能持有旧配置和目录句柄，因此保留
`cmake-build-debug-qt-mingw` 作为临时兼容入口，已使用新依赖重新配置和构建，
并在该入口放置同版 MQTT DLL，避免旧 IDE 环境变量引用已移动的依赖。
重新打开项目后应使用新目录；本轮未关闭用户已有 IDE 或客户端进程。
另一个旧 `cmake-build-debug-qt612-mingw` 已归档。

`.cache/ffmpeg`、`.cache/qt612_validation` 与 `.cache/qt612_migration`
仍包含在用 SDK、验证依赖或运行中的程序，保留原路径。
本轮迁移映射、校验及构建证据在忽略的 `.cache/reorganization/`，不提交机器专用产物。

## 验证

Windows 使用 Qt / Qt MQTT 6.12.0、MinGW 13.1 与 FFmpeg 7 SDK：

- 新构建目录完整构建及最终增量构建通过；`all_qmllint` 无警告。
- 15 组 CTest 全部通过，包含协议、MQTT、视频、日志、崩溃与完整 QML。
- 安装通过；新构建、旧 CLion 兼容入口和隔离开发环境 PATH 的安装程序
  均通过原生 Direct3D 11 启动短测，日志确认 Qt 6.12.0、QML 初始化和三次运行快照，
  无警告或错误，关闭验证窗口后返回 0。

Ubuntu 22.04 / WSL2 使用 Qt / Qt MQTT 6.12.0 与已有软件 FFmpeg 验证环境。
完整构建、`all_qmllint` 和 14 组 CTest 全部通过，安装通过。
测试使用 offscreen/software；HEVC 硬件输出子用例因软件 FFmpeg 跳过，不能作为硬解验收。
清除 `LD_LIBRARY_PATH` 后，安装程序的 `ldd` 未发现缺失库；desktop 文件校验通过，
八档 PNG 和一份 SVG 安装图标与正式资源逐字节一致。
平台插件单独检查仍缺少系统 `libxcb-cursor.so.0`，因此未执行安装程序的原生 X11 启动；
主程序依赖闭包通过不等于显示插件的系统依赖齐备。
首次 QML 检查调度出现过 WSL/NTFS 共享目录约 0.14 秒的时钟差提示，Qt QML 检查本身无诊断；
原始输出保留在本轮验证目录。
最后两项 include 顺序格式调整完成后，相关目标增量重建与 2 组针对性回归再次通过，
该次构建未出现时钟差提示。

本轮使用项目 `.clang-format` 与 Qt 6.12 `qmlformat` 检查迁移源码，
检查 Markdown 相对链接、资源路径及 Git 空白错误。
本轮未重做视频性能测量或长稳；Ubuntu 原生 X11/Wayland、GPU、Intel 核显/NPU
和完整硬件矩阵不在本轮验收结论内。既有平台限制见[Qt 6.12 验收](qt612.md)。
