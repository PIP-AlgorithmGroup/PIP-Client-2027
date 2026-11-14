# 目录约定

目录按职责组织。源码、测试、设计资料与本地产物分别维护；不为尚未实现的功能创建空目录。
新增文件优先归入对应模块，不在仓库根目录另建 `build-功能名` 或堆放临时日志。

```text
PIP-Client/
├── CMakeLists.txt                 # 产品目标、资源与统一构建入口
├── cmake/                         # 依赖检查、应用身份与部署规则
├── src/
│   ├── main.cpp                   # 组合对象与启动入口
│   ├── app/                       # 导航、偏好、字体与比赛会话
│   ├── communication/             # MQTT 数据管线、官方协议表
│   ├── diagnostics/               # 会话日志与运行诊断
│   ├── platform/                  # 原生崩溃取证等系统适配
│   └── video/                     # UDP、组帧、设备、解码与呈交
├── proto/                         # 官方 Protobuf 源定义
├── ui/
│   ├── Main.qml                   # 根窗口、页面及独立叠层的组合
│   ├── components/                # 共享按钮、输入与下拉控件
│   ├── welcome/                   # 欢迎页及背景
│   ├── settings/                  # 客户端菜单和各设置分区
│   └── video/                     # 视频占位与只读调试叠层
├── resources/                     # 产品实际打包的字体、图片、图标及许可
├── tests/
│   ├── CMakeLists.txt             # 测试目标和运行环境
│   ├── app/ communication/        # 应用与通信测试
│   ├── diagnostics/ protocol/     # 日志、崩溃和协议测试
│   ├── video/ ui/ cmake/          # 视频、界面及构建规则测试
│   ├── support/                   # 测试专用 Broker 与视频发包辅助
│   └── fixtures/                  # 固定码流与来源说明
├── packaging/
│   ├── windows/                   # PE 资源模板
│   └── linux/                     # desktop 模板
├── tools/                         # 离线资源制作工具
├── scripts/                       # 仓库维护脚本
├── third_party/
│   ├── sources/                   # 本地依赖源码，忽略
│   └── installed/Windows|Linux/   # 按系统、依赖及版本区分的 SDK，忽略
├── docs/                          # 见文档索引
├── build/                         # 全部忽略；按平台与用途分层
│   ├── windows/qt612-debug/       # Windows 日常构建
│   ├── linux/qt612-debug/         # Ubuntu 构建
│   ├── tools/                     # 独立工具的新构建
│   ├── staging/windows|linux/     # 安装验证暂存
│   └── archive/                   # 历史构建快照，不直接重新构建
└── .cache/                        # 本地 SDK、验证证据和历史归档
```

## 路径与构建

CMake 的入口始终是仓库根目录，测试由 `add_subdirectory(tests)` 管理。
QML 使用显式相对导入共享控件和设置组件，源码测试与打包产品加载同一套文件。
产品的 `qrc:/resources/...`、协议消息、MQTT Topic 和配置存储位置保持原有契约。
源码移动只改变开发路径，不迁移用户设置和日志。

Windows / Ubuntu 各自配置全新的构建树，不搬用另一平台或旧位置的 `CMakeCache.txt`。
例如 `cmake -S . -B build/windows/qt612-debug`，再提供本机 Qt 与依赖前缀；
Ubuntu 使用 `build/linux/qt612-debug`。机器路径仅保留在本地 IDE 配置中。
`PIP_PROTOBUF_ROOT` 默认按 `CMAKE_SYSTEM_NAME` 查找
`third_party/installed/<系统>/protobuf-3.20.3`；其他 SDK 可用显式前缀覆盖。

## 本地产物与历史资料

本轮迁移清单保存在 `.cache/reorganization/mapping.json` 和 `local_moves.json`，
分别记录受版本控制文件与本地产物的原路径和新路径。
历史根目录构建归入 `build/archive`；其中 CMake 保存的绝对路径已过时，
只能用于取证或恢复文件，不能直接运行 `cmake --build`。
历史平铺日志保存在 `.cache/archive/flat`，其他旧验证资料在 `.cache/archive/bundles`。
这些归档内的临时脚本是历史证据，不作为维护中的构建入口。

`.cache/ffmpeg`、`.cache/qt612_validation` 和 `.cache/qt612_migration`
包含仍被使用的 SDK、Linux 依赖或运行中的客户端，因此保留原位置。
它们是有内部依赖的完整包，后续新实验统一写入 `.cache/validation/<主题>`，
不继续往 `.cache` 根目录添加单个日志、截图或临时脚本。

CLion 本地配置使用新的 `build/windows/qt612-debug`。持续打开的 IDE 可能仍持有旧目录；
过渡入口的实际处理与验收范围见[本轮整理记录](validation/directory_reorganization.md)。

## 文档分区

`docs/CODE_STYLE.md` 和 `docs/platform_compatibility.md` 保持固定路径，供开发约束引用。
其他文档按协议、模块、开发、设计、研究和验收分区，入口见[文档索引](README.md)。
