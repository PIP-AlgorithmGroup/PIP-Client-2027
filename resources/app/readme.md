# PIP-Client 应用图标

三套原创图形标识分别存放在 `variants/viewlink`、`variants/circuit`、`variants/signal`。
它们对应画面链路、互连终端和竞技信号，不含官方标志或第三方素材。
每套 SVG 是唯一图形源，PNG 与 ICO 均由 `tools/generate_app_icons.py` 生成。

默认构建选择 circuit，可在 CMake 配置时设置 `-DPIP_APP_ICON_VARIANT=viewlink`、
`circuit` 或 `signal`，统一切换窗口、可执行文件与 Linux 桌面图标。

使用 Python 3.9 及以上和 Pillow 9.1 及以上运行，无需网络或 Qt 安装。
当前资源由 Pillow 13.0.0 生成；要求逐字节复现时固定同一 Pillow 版本：

```sh
python tools/generate_app_icons.py --variant circuit
```

生成 PNG：16、24、32、48、64、128、256、512 像素；
Windows ICO 包含前七档 32 位透明 PNG 图层。
生成器仅接受本图标使用的 `rect`、`polygon`、纯色填充与固定画布，
遇到不支持的 SVG 元素会失败，避免升级图稿后静默生成错误资源。
运行时直接加载打包 PNG，避免依赖系统 SVG 图像插件。

Linux 桌面 ID 为 `org.pip.PIPClient`，图标名相同；
`StartupWMClass=PIPClient` 对应既有 Qt `applicationName`，保持用户配置路径。
Windows 文件和产品版本统一来自 CMake `PROJECT_VERSION`，无需手动改资源模板。
生成文件属于应用资源并随源码保存，普通构建不要求 Python 或 Pillow。
