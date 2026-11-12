# RoboMaster 2027 自定义终端协议

## 来源与范围

当前协议基线为《RoboMaster 2027 机甲大师高校系列赛通信协议 V1.0.0（20261029）》。
官方修订日志记载 2026-10-29 首次发布。适用于 RMUC 和 RMUL 的共用条目；单一赛项条目不自动适用于另一赛项。

- [官方 PDF](RoboMaster%202027%20机甲大师高校系列赛通信协议%20V1.0.0（20261029）.pdf)：权威来源。
- [终端发送定义](../proto/client_to_server.proto)：9 个 MQTT 指令。
- [终端接收定义](../proto/server_to_client.proto)：23 个 MQTT 指令和 1 个雷达辅助消息。
- [字段快照](../tests/protocol_2027_schema.txt)：逐项核对第 2.2 节，供离线回归检查。
- [2026 归档](protocol_2026_archive.md)：保留旧赛季资料，不参与当前实现。

本文中的“官方规定”指 PDF 正文；“工程选择”指 PIP-Client 的实现决定。未给出的单位、
枚举值、默认值、数组顺序和时间基准不得自行补充。代码只维护现有项目需要的自定义终端定义，
已有独立的 [通用 MQTT 接收链路](data_pipeline.md)，未绑定业务订阅和 UI 字段；
RMUL 已实现 UDP 视频接收；尚未实现指令发送或串口。

## 相对旧项目基线的变化

旧项目采用 2026 V2.0.0（20260626）。跨赛季相同字段号不保证相同业务含义，
不能通过 Protobuf 解析成功来判断协议版本兼容。

| 范围 | 2027 变化 |
| --- | --- |
| CustomControl | 上限由 30 增为 60 字节，对应图传命令 0x0311 |
| GameStatus | end_reason 改为基地摧毁、控制度、总伤害、总血量、平局、异常终止、主裁手动判定 |
| GlobalUnitStatus | 4/5、9/10 改为固定 R/B 前哨；base_shield 与 enemy_base_shield 表示阵地控制状态 |
| GlobalUnitStatus | 新增 total_CP_ally = 15、total_CP_enemy = 16；血量数组改为双方各 1、3、4、7 号 |
| GlobalSpecialMechanism | 更新堡垒激活描述，增加 R/B 前哨与 R/B 基地阵地计时（3–6） |
| Event | 5–8 改为阵地控制事件，14 改为重装战亡临时激活，新增 16 模块化 42 mm 发射机构安装 |
| RobotDynamicStatus | 字段 4 改名 reserved，保留位不再表示底盘能量 |
| RobotModuleStatus | 字段 11 改为 modular_42mm，表示重装模块化 42 mm 发射机构安装权限 |
| RobotStaticStatus | 发射机构和底盘性能体系均只列出 1、2，不再沿用旧英雄的 3、4 |
| RadarInfoToClient | 机器人顺序改为对方 1、3、4、7，再己方 1、3、4、7；外层定义歧义仍在 |
| RobotPosition | 坐标归属说明改为指挥手客户端的哨兵 7/107 |
| 消息删除 | HeroDeployModeEventCommand、DeployModeStatusSync、AirSupportCommand、AirSupportStatusSync 不在 2027 清单中 |
| ID 与称谓 | 1/101 为重装，2/102 及 0x0102/0x0166 为保留位；6/106 为空中系统 |

官方字段 outpost_R_health、outpost_R_status、outpost_B_health、outpost_B_status、
total_CP_ally、total_CP_enemy 保留大小写。原字段号按新版原文更新，不为旧赛季保留业务别名。

## 网络与编码（官方 §2）

| 项目 | 定义 |
| --- | --- |
| MQTT 服务器 | 192.168.12.1:3333 |
| 自定义终端地址 | 192.168.12.2 |
| MQTT 负载 | 直接传输 Protobuf v3 序列化二进制，不封装为 JSON 或文本 |
| MQTT topic | 对应指令名，大小写精确匹配，不添加 package 前缀 |
| Protobuf 要求 | 3.15 及以上，保留 proto3 optional |
| MQTT clientID | 第 2 节写明为所连接机器人对应的 ID 编号；不能沿用旧整理中“必为选手端 ID”的推断 |
| 最高 QoS | 表 2-1 的所有指令均为 1 |

字段缺省与显式零值、false、空字节串可以通过 proto3 optional 区分。
这种存在性是 Protobuf 语言语义，不能自动解释成官方规定的业务默认值。

附录三示例默认地址为 127.0.0.1:3333，发布函数默认使用 ExactlyOnce（QoS 2）。
比赛连接应服从第 2 节的网络地址及表 2-1 的最高 QoS 1。
QoS 1 仅覆盖 MQTT 交付，不能替代业务执行结果；哨兵请求需结合 SentryCtrlResult 及状态消息判断。

## 指令总览（官方表 2-1）

以下所有 MQTT 指令最高 QoS 均为 1；频率来自官方表格，不能将触发式误解为不限频。

| Topic | 方向 | 频率或上限 |
| --- | --- | --- |
| KeyboardMouseControl | 终端 → 图传 → 机器人 | 75 Hz |
| CustomControl | 终端 → 图传 → 机器人 | 75 Hz |
| GameStatus | 服务器 → 终端 | 5 Hz |
| GlobalUnitStatus | 服务器 → 终端 | 1 Hz |
| GlobalLogisticsStatus | 服务器 → 终端 | 1 Hz |
| GlobalSpecialMechanism | 服务器 → 终端 | 1 Hz |
| Event | 服务器 → 终端 | 触发式 |
| RobotInjuryStat | 服务器 → 终端 | 1 Hz |
| RobotRespawnStatus | 服务器 → 终端 | 1 Hz |
| RobotStaticStatus | 服务器 → 终端 | 1 Hz |
| RobotDynamicStatus | 服务器 → 终端 | 10 Hz |
| RobotModuleStatus | 服务器 → 终端 | 1 Hz |
| RobotPosition | 服务器 → 终端 | 1 Hz |
| Buff | 服务器 → 终端 | 获得时触发，此后 1 Hz 直至失去增益 |
| PenaltyInfo | 服务器 → 终端 | 触发式 |
| RobotPathPlanInfo | 服务器 → 终端 | 触发式 |
| MapClickInfo | 服务器 → 终端 | 触发式，限制见 §1.3.1 |
| MapClickCmd | 终端 → 服务器 | 触发式，限制见 §1.3.1 |
| RadarInfoToClient | 服务器 → 终端 | 触发式 |
| CustomByteBlock | 机器人 → 图传 → 终端 | 50 Hz |
| AssemblyCommand | 终端 → 服务器 | 触发式，最高 10 Hz |
| TechCoreMotionStateSync | 服务器 → 终端 | 1 Hz |
| RobotPerformanceSelectionCommand | 终端 → 服务器 | 触发式，最高 10 Hz |
| RobotPerformanceSelectionSync | 服务器 → 终端 | 1 Hz |
| CommonCommand | 终端 → 服务器 | 触发式，最高 10 Hz |
| RuneActivateCommand | 终端 → 服务器 | 触发式，最高 10 Hz |
| RuneStatusSync | 服务器 → 终端 | 1 Hz |
| SentryStatusSync | 服务器 → 终端 | 1 Hz |
| DartCommand | 终端 → 服务器 | 触发式，最高 10 Hz |
| DartSelectTargetStatusSync | 服务器 → 终端 | 1 Hz |
| SentryCtrlCommand | 终端 → 服务器 | 触发式，最高 10 Hz |
| SentryCtrlResult | 服务器 → 终端 | 触发式 |

RadarSingleRobotInfo 是 §2.2.19 的辅助结构，没有独立 topic、频率或 QoS。

## 关键字段与约束

完整字段名、类型、编号、数组及注释见两份 proto 文件。以下记录容易误用的跨消息语义。

### 比赛与阵地

GameStatus.current_stage：0 未开始，1 准备，2 十五秒自检，3 五秒倒计时，4 比赛中，5 结算。
game_result 结算时为 0 平局、1 红胜、2 蓝胜，其他阶段发 255。
end_reason 仅结算有效，其他阶段发 255；结算时取值 1–7，按前述变更表解释。

R/B 指开局时红/蓝方一侧的前哨、阵地，之后不随控制方改变。
GlobalUnitStatus 中基地、伤害和控制度的己方/对方仍相对终端所属队伍。
base_shield 为 1 己方完全控制、2 对方临时控制；enemy_base_shield 为 1 对方完全控制、2 己方临时控制。
两处前哨阵地状态为 1 己方完全控制、2 己方临时控制、3 对方临时控制、4 对方完全控制。
robot_health 共 8 项，顺序为己方 1、3、4、7，再对方 1、3、4、7。
robot_bullets 的新版本条目未明确数组长度和机器人顺序，不从血量数组推导。

GlobalSpecialMechanism 的两个数组按索引对应。机制 1/2 为双方堡垒被对方激活计时；
3/4 为 R/B 前哨阵地，5/6 为 R/B 基地阵地。机制 3–6 的时间参数单位为秒，
小于 0 为对方推进、大于 0 为己方推进、0 为状态无变化。

### 事件（官方 §2.2.7）

| event_id | 事件 | param |
| --- | --- | --- |
| 1 | 击杀 | 被击杀者 ID、击杀者 ID，逗号分隔 |
| 2 | 前哨被摧毁 | 11 为 R 前哨，111 为 B 前哨 |
| 3 | 大能量机关激活结果 | 激活灯臂数、平均环数，如 10,9.6 |
| 4 | 能量机关进入已激活状态 | 1 小能量机关，2 大能量机关 |
| 5 | R 前哨阵地改变控制状态 | 1 红临时、2 红完全、3 蓝临时、4 蓝完全 |
| 6 | B 前哨阵地改变控制状态 | 编码同事件 5 |
| 7 | R 基地阵地改变控制方 | 1 红完全、2 蓝临时 |
| 8 | B 基地阵地改变控制方 | 1 蓝完全、2 红临时 |
| 9 | 飞镖命中 | 命中方 1 红/2 蓝，及目标 1 前哨/2 基地固定/3 随机固定/4 随机移动/5 末端移动 |
| 10 | 对方飞镖闸门开启 | 无参数 |
| 11 | 基地遭到攻击 | 无参数；5 秒内置冷却 |
| 12 | 对方前哨停转 | 无参数 |
| 13 | 对方基地护甲展开 | 无参数 |
| 14 | 重装战亡临时激活，进入强制退出缓冲期 | 文档未定义参数格式 |
| 15 | 装配结果 | 0 成功、1 拔出、2 超时、3 离开过久、4 重装战亡、5 四级协作超时、6 主动退出、7 未检测能量单元、8 缓冲期到期强制结束 |
| 16 | 重装成功安装模块化 42 mm 发射机构 | 文档未定义参数格式 |

事件 5–8 的参数使用固定红蓝方编码，不同于 GlobalUnitStatus 的己方/对方编码。

### 输入、地图、自定义数据

键盘 bit 0–15 依次为 W、S、A、D、Shift、Ctrl、Q、E、R、F、G、Z、X、C、V、B，1 为按下。
mouse_x 负值向左、mouse_y 负值向下、mouse_z 负值向后。
比赛时键鼠输入来源只能选择官方选手端或自定义终端之一。

MapClickCmd/MapClickInfo 的 robot_id 固定 7 字节；例如发给重装和哨兵，
红方为 1、7 后补零，蓝方为 101、107 后补零。
自定义图标仅 mode = 4 有效，ASCII 字母范围为 C–L、N、O、Q–Z。
触发间隔按 §1.3.1：云台手至少 0.5 秒，半自动机器人操作手至少 3 秒。
该节对串口 0x0303 的额外发送和重发描述不自动构成 MQTT 发布重试要求。

CustomControl 上限 60 字节，对应 0x0311；CustomByteBlock 上限 2.4 kbit（300 字节），对应 0x0310。
两条图传交互协议无重传。Protobuf bytes 类型不会自动执行长度上限；
接入收发边界时必须在发布前和接收后校验，不能把离线序列化通过当作业务校验通过。

RobotPathPlanInfo 起点单位为分米，X/Y 增量数组各 49 项，每项范围 −128 至 127，
采用 packed int32。两处原文对增量基准的冲突见下节。

### 机器人与控制

RobotStaticStatus 发射机构性能体系为 1 冷却优先、2 爆发优先，底盘为 1 血量优先、2 功率优先。
RobotDynamicStatus.reserved 不参与能量展示；experience_for_upgrade 是距离升级仍需的经验。
RobotModuleStatus 字段 1–10 为 0 离线、1 在线、2 安装不规范视为离线；
modular_42mm 为 0 无权限、1 有权限，仅重装有效。

RobotPerformanceSelectionCommand 的 sentry_control 为 0 自动、1 半自动。
CommonCommand 类型 1 兑换 17 mm 发弹量（参数为 10 的倍数）、2 兑换 42 mm 发弹量，
两者须金币足够；3 确认复活（读条完成时立即复活）、4 兑换立即复活、5 远程兑换发弹量、
6 远程兑换血量，后 3 项均须符合规则要求。

SentryCtrlCommand：0 无效，1 补血点补弹，3 远程补弹，4 远程回血，5 确认复活，
6 金币复活，7/8/9 进攻/防御/移动姿态，10/11/12 对应强化姿态。SentryCtrlResult 的 0 为成功，其他为失败。
DartCommand 目标编号沿用事件 9 的五类目标；launch_confirm 官方默认 0，1 确认发射。

## 图传 UDP（官方 §2）

官方网络约定：MQTT 服务器 `192.168.12.1:3333`，自定义终端本机 IP `192.168.12.2`。
UDP 视频是另一条接收链路，终端在本地绑定 3334 端口等待数据；不通过 MQTT 订阅视频，
不向 3333 端口请求视频。本节未单独指定 UDP 发送源 IP/源端口，不把 MQTT 地址当作已确认的 UDP 来源。

终端监听 UDP 3334。码流为标准 H.265/HEVC，1920 × 1080、60 帧，无重传。
每包最多 1400 字节，头部 8 字节采用大端编码，负载最多 1392 字节：

| 偏移 | 长度（字节） | 含义 |
| --- | --- | --- |
| 0 | 2 | 递增帧编号 |
| 2 | 2 | 当前帧内分片序号 |
| 4 | 4 | 当前帧总字节数 |
| 8 | 最多 1392 | HEVC 码流 |

工程接入时需处理长度异常、分片丢失/重复/乱序、帧号回绕及超时，避免不完整帧进入解码器。
官方本节没有指定分片序号起始值和重组超时，不能当作协议默认值。

RMUL 已实现 UDP 分片重组和 FFmpeg HEVC 软件解码；分片起始序号可配置。
工程超时、资源上限、来源隔离与验证边界见[图传接收](video_receiver.md)，尚未宣称官方设备互通。

## ID（官方附录二）

| 对象 | 红方 | 蓝方 |
| --- | --- | --- |
| 重装 | 1 | 101 |
| 保留位 | 2 | 102 |
| 步兵 | 3、4、5 | 103、104、105 |
| 空中系统 | 6 | 106 |
| 哨兵 | 7 | 107 |
| 飞镖 | 8 | 108 |
| 雷达 | 9 | 109 |
| 基地 | 10 | 110 |
| R/B 前哨 | 11（R） | 111（B） |

选手端 ID：红方重装 0x0101、步兵 0x0103–0x0105、空中系统 0x0106；
蓝方重装 0x0165、步兵 0x0167–0x0169、空中系统 0x016A。
0x0102/0x0166 为保留位；0x8080 为裁判系统服务器，用于哨兵和雷达自主决策指令。
附录列出空中系统 ID 不代表旧 AirSupport 消息仍可用。

## 原文歧义与工程处理

### 雷达外层布局未确定

§2.2.19 表格写 repeated uint32，代码片段写 repeated uint32 RadarSingleRobotInfo，
却缺少字段号，后面又单独定义 RadarSingleRobotInfo 消息。
这些内容不能唯一决定外层是扁平 uint32 数组还是 repeated RadarSingleRobotInfo。
内层三个字段定义明确：target_pos_x = 1、target_pos_y = 2（均为 cm），is_high_light = 3
（0 无标识、1 有标识、2 有标识但定位离线）。

**工程选择：**外层暂保留旧工程 repeated uint32 RadarSingleRobotInfo = 1，
并补齐独立的 RadarSingleRobotInfo 辅助定义。外层字段号 1 来自表格数据编号和旧工程，
不是 PDF 中完整有效的 Protobuf 声明；不得宣称该选择已与官方服务端互通。
数组位置只确认对方 1、3、4、7，再己方 1、3、4、7；不推断扁平化方法。
在接入该 topic 前，应以官方勘误、正式 proto 或服务端抓包确认。确认后同步修改定义、快照和测试。

这是对 CODE_STYLE.md §7.2–7.3“以权威定义为准”的受限例外：
原因是官方缺少唯一有效的外层声明；影响仅限 RadarInfoToClient；上述确认材料到位时重新评估。

### 路径增量基准冲突

§2.2.16 描述相对起始点；§1.3 表 1-37 描述相对上一个点位。
当前只提供原始字段，不实现坐标还原，不将任一解释默认为全链路语义。

### 其他不完整定义

§2 正文仅要求 clientID 使用所连接机器人对应的 ID 编号，
没有说明是否转换为附录选手端 ID，也没有规定编号的字符串编码；收发接入前需核实。
原文未提供的坐标单位、时间单位和数组顺序保留未定义状态，不能从其他消息借用。

## 构建与验证

工程固定 protoc 3.20.3 和 Protobuf C++ runtime 3.20.3（BSD-3-Clause），满足官方 3.15 以上要求。
CMake 从选定 Protobuf 安装包的 protobuf::protoc 导入目标获取可执行文件，
配置时检查版本不一致则失败，不从开发机 PATH 临时寻找 protoc。
生成文件仅位于构建目录的 generated/。

PIPClient 和 protocol_2027_test 链接同一 pip_protocol 静态库。
正常构建后执行：

```text
cmake --build <构建目录>
ctest --test-dir <构建目录> --output-on-failure
```

测试覆盖完整消息清单、字段名/号/类型、optional 存在性、repeated packed 编码，
固定二进制向量中的 R/B 前哨与控制度、60 字节二进制数据、负增量、未知字段和截断输入。
它不替代 MQTT 联调、频率和长度边界校验、UDP 重组、硬件或比赛现场验证。
