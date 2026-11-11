# 图传监看与客户端菜单设计参考

调研日期：2026-11-11。范围为 QGroundControl、Mission Planner 和 OBS Studio 的官方文档、
官方截图与公开源码；以下将参考软件的事实和 PIP-Client 的设计建议分开记录。
本次没有运行参考软件做实机验收，不据此推断其所有平台或断流行为。

## 官方参考事实

### QGroundControl：画面占位与应用设置分层

- 无视频组件使用 `NoVideoBackground.jpg` 背景图，保持比例裁切；其上居中叠加
  `WAITING FOR VIDEO` 或 `VIDEO DISABLED`。背景原图是蓝天、白云和绿色田野航拍照片，
  并非技术网格或纯黑界面。
  来源：[NoVideoPlaceholder.qml](https://github.com/mavlink/qgroundcontrol/blob/13faea0245c22a1d763b96721fe5c1711edc52e2/src/VideoManager/Qml/NoVideoPlaceholder.qml)、
  [官方背景原图](https://raw.githubusercontent.com/mavlink/qgroundcontrol/13faea0245c22a1d763b96721fe5c1711edc52e2/resources/NoVideoBackground.jpg)。
- 状态标签采用白色粗体文字、半透明黑色矩形衬底，并支持不同字号以适配画面尺寸。
  来源：[VideoStatusLabel.qml](https://github.com/mavlink/qgroundcontrol/blob/13faea0245c22a1d763b96721fe5c1711edc52e2/src/VideoManager/Qml/VideoStatusLabel.qml)。
- 当前视频视图支持保持比例显示和裁切策略。断流重连时会保留最后帧，同时显示
  `VIDEO LOST - RECONNECTING`，代码明确指出需要防止用户把冻结帧误认为实时画面。
  这是该参考软件的策略，不是 PIP-Client 当前策略或本次变更要求。
  来源：[VideoView.qml](https://github.com/mavlink/qgroundcontrol/blob/13faea0245c22a1d763b96721fe5c1711edc52e2/src/VideoManager/Qml/VideoView.qml)。
- 应用设置采用左侧分类、右侧内容区域，支持子分类和搜索；Video 是设置分类之一，
  同层还有 General、Comm Links、Maps、Telemetry 等。
  来源：[AppSettings.qml](https://github.com/mavlink/qgroundcontrol/blob/13faea0245c22a1d763b96721fe5c1711edc52e2/src/QmlControls/AppSettings.qml)、
  [SettingsPages.json](https://github.com/mavlink/qgroundcontrol/blob/13faea0245c22a1d763b96721fe5c1711edc52e2/src/AppSettings/pages/SettingsPages.json)。

可借鉴的是：无画面状态使用完整画面构图，状态文字需要明确且有衬底；视频配置从属于应用设置。
其飞控 HUD、遥测覆盖和最后帧保留策略不属于本项目本次需求。

### Mission Planner：无视频仍有明确的监看画幅

- 官方 Live Video 文档说明，视频既可放在 HUD，也可占用地图区域；Gimbal Video 提供
  Full Sized、Mini、Pop Out 三种显示入口。新建视频区域显示 `No Video`，
  通过该区域右键菜单中的 Video Stream 进入视频源配置。
  来源：[官方 Live Video 文档](https://ardupilot.org/planner/docs/live-video.html)。
- 文档的无视频截图采用大面积黑色视频区域、暗红斜线组成的 X，以及带白色边框的
  `No Video` 文字。画面区域与右键配置入口有清晰层次。
  来源：[官方无视频截图](https://ardupilot.org/planner/_images/mp-map-video-menu.png)、
  [地图和视频布局截图](https://ardupilot.org/planner/_images/mp-map-video.png)。
- Flight Data 文档区分应用配置中的摄像头来源与 HUD 局部视频控制，说明视频局部操作
  与应用全局功能具有不同职责。
  来源：[官方 Flight Data 文档](https://ardupilot.org/planner/docs/mission-planner-flight-data.html)。

可借鉴的是：无画面也要让用户识别出监看区域和当前状态。其红叉、飞控 HUD 和旧式菜单外观
不作为 PIP-Client 的视觉模板。

### OBS Studio：设置按应用能力组织

- 官方概览将 Settings 分为 General、Stream、Output、Audio、Video、Hotkeys、Advanced；
  设置截图显示左侧图标分类、右侧具体参数，Video 只占其中一类。
  来源：[官方概览](https://obsproject.com/kb/obs-studio-overview)、
  [官方设置截图](https://obsproject.com/media/pages/kb/obs-studio-overview/97ba90aa9a-1767490236/g1jtqud-1.png)。
- Studio Mode 将预览与节目输出放在不同区域。此处仅用于理解工作区与控制区的层次，
  不据此建议本项目增加双预览。
  来源：[官方概览中的 Studio Mode 说明](https://obsproject.com/kb/obs-studio-overview)。

本次未核实 OBS 网络源的断流占位行为，不将其描述为本项目无信号画面的直接参考。

## 对 PIP-Client 的设计建议

以下是根据用户需求形成的原创设计建议，不声称是上述软件的原样布局。

### 主图传无画面状态

使用完整监看器构图替代中央小图标卡片：较亮的冷灰底色、低对比网格、16:9 参考画幅与角标，
配合有尺度感的 `NO SIGNAL` 字样、准确中文状态和真实监听端点。参考画幅只表达待机视觉，
不规定实际输入视频必须是 16:9。

用户应能区分等待输入、暂停、启动失败和断流等真实状态；具体区分以接收后端已经提供的
状态为依据，不通过动画、假帧率、假遥测或假进度制造“正在工作”的错觉。待机背景应明确为
非实况，避免用照片冒充收到的视频。连接恢复后的画面显示沿用既有状态机制，不为了视觉设计
改变缓存、线程或重连语义。

收到有效视频后隐藏整套待机视觉，保持视频居中、等比缩放，必要时留边；不裁切、不拉伸，
不添加比赛 HUD 或其他未设计的覆盖层。

### Esc 是客户端菜单

菜单先表达客户端层级，再进入图传配置：可用概览或菜单首页作为默认入口，左侧组织图传、
已存在的显示选项和可用诊断内容，右侧展示所选内容。“继续画面”和“返回欢迎页”是全局动作。
仅展示真实可用功能；不为了填满分类而增加尚未实现的 MQTT、设备管理或战术页面。

图传分区保留基础 UDP 订阅配置和可解释的连接状态，不把整个 Esc 页面命名为“图传设置”。
入口层级和面板标题需要一致，键盘 Esc 的打开、关闭及编辑行为由本项目定义，不能写成
参考软件的既有快捷键规则。

### 尺寸与平台约束

UI 大、中、小三档决定文字与控件尺寸，窗口变化只调整位置、留白和内容区域；
小窗口通过合理换行、滚动和收敛留白避免溢出，不随窗口任意缩小文字。

优先使用现有 Qt Quick 的矩形、渐变、线条、文字和少量 SVG，不新增着色器、3D、厂商 GPU
或视频硬解依赖。待机图形保持静态或只使用必要的轻量状态变化；软件渲染、Intel 核显和
NVIDIA 设备均不应因这次视觉改造增加启动门槛。此为兼容性设计目标，Windows/Ubuntu
与各驱动的实测结果仍应单独记录，不能把设计目标等同于已完成验收。

不直接打包参考软件截图或背景资源；它们仅作为调研依据。项目当前平台边界见
[平台与硬件兼容约束](../platform_compatibility.md)，视频接收语义见
[图传接收](../video_receiver.md)。
