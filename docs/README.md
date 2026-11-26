# 文档索引

| 分区 | 内容与入口 |
| --- | --- |
| 开发约束 | [代码规范](CODE_STYLE.md)、[平台兼容](platform_compatibility.md) |
| 项目结构 | [模块架构](ARCHITECTURE.md)、[目录约定](directory_layout.md) |
| 资源 | [资源总览](resource_index.md)：产品素材、官方档案、设计记录与开发辅助资源的按需入口 |
| 协议 | [2027 协议](protocol/protocol_2027.md)、`protocol/reference/` 官方原文、[2026 归档](protocol/protocol_2026_archive.md) |
| 模块 | [数据管线](modules/data_pipeline.md)、[控制器串口](modules/serial_controller.md)、[图传接收](modules/video_receiver.md) |
| 开发与部署 | [本地接入](development/local_integration.md)、[串口发送与解码工具](development/serial_tools.md)、[日志与桌面部署](development/diagnostics_and_deployment.md)、[发行流程](development/releases.md) |
| 设计 | [内部拓扑](design/network/topology.md)、[键鼠控制架构](design/keyboard_mouse_input.md)、[欢迎页](design/welcome/design.md)、`design/rmul/`、`design/icons/` |
| 研究 | `research/video/`：开源接收端、解码与 Qt 渲染调度的来源调查 |
| 验收 | [目录整理](validation/directory_reorganization.md)、[Qt 6.12](validation/qt612.md)、[控制器串口](validation/serial_controller.md)、[欢迎页](validation/welcome.md)、[RMUL](validation/rmul.md)、[GPU 视频通路](validation/video/gpu_pipeline.md)、[后台呈交](validation/video/background_presentation.md)、[独立视频呈交](validation/video/independent_presentation.md)、[GUI/后端边界](validation/video/gui_backend_separation.md)、`validation/video/` |

设计和研究记录不等于功能已经实现；构建、离屏回归、原生桌面和硬件验收分别记录。
历史版本的测试数据保留当时版本与限制，不替换成当前版本的验证结论。

发行记录：[v0.1.0-alpha.1](releases/v0.1.0-alpha.1.md)，
[发行验收](validation/release_alpha1.md)；[v0.2.0-alpha.1](releases/v0.2.0-alpha.1.md)；
[v0.3.0-alpha.1](releases/v0.3.0-alpha.1.md)；[v0.4.0-rc.1](releases/v0.4.0-rc.1.md)。

- [原生键鼠控制与测试](modules/keyboard_mouse_input.md)
- [键鼠验证记录](validation/keyboard_mouse_input.md)
- [手动软件更新](development/updates.md)与[更新验证](validation/updates.md)
