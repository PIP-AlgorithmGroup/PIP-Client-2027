# 自定义控制器发送与图传串口解码工具

本工具用于验证 Python → 串口 → PIP-Client → MQTT `CustomControl` → MockServer →
串口 `0x0311` → 上位机解码。发送和接收共用 `tools/serial_control.py`，
纯协议模块为 `tools/serial_protocol.py`。默认不连接、不发送。

## 启动

需要 Python 3.10+（含 Tk）。Windows 双击 `tools/run_serial_control.cmd`，
首次在仓库 `.cache/serial_tools/venv` 创建环境并安装固定依赖，需要网络；
之后复用该环境，不修改系统 PATH 或全局 Python 包。

Ubuntu 需要发行版的 `python3-tk` 和 `python3-venv`，以及串口设备读写权限
（通常由 `dialout` 组管理，重新登录后生效）。工具不自动提权或改设备权限。
在仓库根目录执行：

```sh
python3 -m venv .cache/serial_tools/venv
.cache/serial_tools/venv/bin/python -m pip install -r tools/requirements_serial.txt
.cache/serial_tools/venv/bin/python tools/serial_control.py
```

pyserial 固定 3.5（BSD-3-Clause），paho-mqtt 固定 2.1.0（EPL-2.0 / EDL-1.0），
后者仅用于整链路验收脚本；Tk 使用 Python 自带界面库，无新增产品运行依赖。

## 界面操作

1. 刷新设备并选择串口，也可手填 `COM4` 或 `/dev/serial/by-id/...` 稳定路径。
   波特率、数据位、校验、停止位、流控均可配置，默认 **921600 / 8N1 / none**。
   活动连接禁止修改参数；断开后重新连接生效。
2. 原始 HEX 填写 **数据段**，例如 `00 A5 FF 00`，工具自动封装 `0x0311`、序号和 CRC。
   允许空负载，最大 60 字节；不填串口帧头，不补齐或移除尾零。
3. 点击“发送一帧”，或填写频率（1..75 Hz，默认 50）后开始周期发送。
   周期中序号递增并在 255 后回到 0；修改负载前先停止。
   工作线程独占串口，命令槽最多一个，不积压、不追赶补发；写失败终止连接。
4. 勾选“发送 PIP1 四轴样例”时，改为发送下表中的工程调试负载。
   `buttons` 可填 `0x15`，四轴填有限浮点数；`counter` 周期递增。
5. 接收端只需连接，无需发送。界面显示命令、序号、长度、原始负载和累计计数；
   勾选“接收按 PIP1 样例解析”显示四轴与按键。其他负载保留 HEX 并显示样例不匹配原因。
   “期望负载 HEX”与最近数据段逐字节比较，留空表示期望零长度。
6. 停止发送后可保持接收；断开或关闭窗口会停止并回收串口线程，释放端口。
   断线不自动重连或重放控制。日志最多 120 行，显示队列最多 200 条；显示拥塞单独计数。

PIP1 **不是官方规定的内部字段**。官方只规定数据段容量及外层帧；真实自定义控制器
应使用双方约定的数据段布局，界面原始 HEX 可发送任意符合容量的布局。

| 字节偏移 | 长度 | PIP1 样例字段 |
| --- | --- | --- |
| 0 | 4 | ASCII `PIP1` 标记 |
| 4 | 4 | `counter`，uint32，小端 |
| 8 / 12 / 16 / 20 | 各 4 | `x / y / yaw / pitch`，float32，小端，无预设物理单位 |
| 24 | 4 | `buttons`，uint32 位掩码，小端 |

总长 28 字节。跨端只比较数据段；MockServer 为输出串口帧独立维护序号，
不能要求它与输入端序号相同。监测解码器保留重复序号用于诊断，产品控制入口另有去重策略。

## 接线与 MockServer 配置

同时验证两段实体串口，需要两对互连端口：

```text
发送工具 ─ USB串口A ↔ USB串口B ─ PIP-Client
                                 │ CustomControl.data（原始数据段）
                                 ▼ MQTT
解码工具 ─ USB串口D ↔ USB串口C ─ MockServer
```

每对 TTL 串口交叉 TX/RX 并共地，使用匹配电平。串口由持有进程独占。
只有 COM4/COM5 时，可先关闭 MockServer 串口，由发送器占 COM4、客户端占 COM5；
验证 MQTT 后停止发送并释放两端，再由 MockServer 占 COM4、解码器占 COM5。
这属于分段实体测试；要连续运行，输入段可使用 PTY，或增加第二对硬件。

客户端需先连接 MQTT 并进入赛事，再在设置页连接控制器串口。
MockServer 已有相应桥接，无需本次修改其代码。在其页面按顺序：

1. 启动 broker，或准备已有可达 broker。
2. 打开输出串口，设 921600 / 8N1 / 无流控。
3. 启动 MockServer 内部 MQTT，连接客户端使用的同一 broker，等待订阅就绪。
4. 启动图传桥接，使 `CustomControl` 转成 `0x0311`。
5. 在另一端运行接收工具，再发送新控制。保留消息、离线旧数据不作为新控制重放。

## 命令行

以下 `python` 指安装好依赖的环境；Windows 也可将其替换为
`.cache/serial_tools/venv/Scripts/python.exe`，Ubuntu 使用对应 `bin/python`。

```sh
# 独占一对互连硬件，双向各测试30帧；会打开两端，请先释放其他进程的串口。
python tools/serial_control.py --pair-test COM4 COM5

# 发送一帧；接收端应提前启动。
python tools/serial_control.py --port COM4 --send-hex "00 A5 FF 00" --seq 0

# 接收30秒并逐帧校验0311数据段；不足1帧或负载不符返回非零退出码。
python tools/serial_control.py --port COM5 --receive --seconds 30 --expect-hex "00 A5 FF 00"

# 显式解析PIP1；去掉--demo可接收任意原始数据段。
python tools/serial_control.py --port COM5 --receive --seconds 30 --demo --min-frames 1
```

接收 stdout 为逐行 JSON，包括 `command`、`seq`、`length`、`hex` 和 `frame_hex`；
汇总计数输出 stderr。每次读取不等于一帧，解析器支持半包、粘包、噪声、CRC 错误恢复
与 200 ms 残帧超时。支持 `0x0311`（最多 60B）及 `0x0310`（最多 300B）解码。

官方 A5 帧按 2027 V1.0.0：`A5 | length LE16 | seq | CRC8 | cmd LE16 | payload | CRC16 LE16`。
CRC8 为 init FF / reflected 8C；A5 CRC16 为 init FFFF / reflected 8408，无最终异或。
MockServer 还周期输出 `A9 53` 遥控帧，工具按独立帧识别，默认不打印但 UI 保留计数。
`--show-remote` 可输出遥控 JSON。遥控 CRC 默认 `ccitt_false`（VT03/VT13 手册）；
只有对端明确采用另一官方样例时才选 `--remote-crc official_sample`。
该选择只影响遥控帧，不影响 A5；不自动猜测或切换 CRC。

## 自动验收与硬件探针

```sh
python tests/tools/serial_tools_test.py
python tests/tools/serial_chain_test.py
```

普通协议测试不打开硬件；设置 `PIP_SERIAL_UI_TEST=1` 增加真实 Tk / pyserial loop 测试，
设置 `PIP_SERIAL_UI_PORTS=COM4,COM5` 才启用双窗口实体收发测试。Ubuntu 可用同样的
环境变量和设备路径，需要可用显示服务。Windows PowerShell 使用 `$env:名称='值'`。

CMake 的 `serial_pipeline_probe` 目标链接实际产品 `SerialController` / `DataPipeline`，
只写临时配置目录，默认 20 秒退出，不注册为自动 CTest。可用 `--help` 查看参数。

```sh
cmake --build build/windows/qt612-debug --target serial_pipeline_probe
python tools/test_serial_chain.py --tx COM4 --rx COM5 \
  --probe build/windows/qt612-debug/serial_pipeline_probe.exe \
  --mockserver ../PIP-MockServer/build/windows-debug/bin/pip_mockserver.exe \
  --output .cache/validation/serial_tools/manual
```

上述为多行 shell 表达；PowerShell 可合并为一行。probe 运行环境需能找到对应 Qt、MQTT、
SerialPort 和编译器运行库；Ubuntu 可执行文件通常在构建目录 `tests/` 下。
验收脚本启动独立 MockServer 和临时 broker，不连接或停止用户已有服务；
先验证实体串口到产品 MQTT，再重放实际捕获的 protobuf 到 MockServer 并核对实体输出。
在 Windows 可增加 `--linux-probe`、`--linux-workspace`、`--linux-library-path`
及 `--wsl-distribution`，用 WSL PTY 输入追加连续链路，详见脚本 `--help`。
后端、probe 和 PTY 源均有有界生命周期；退出释放本轮设备和 broker。

实测结果及限制见[串口验收记录](../validation/serial_controller.md)。
