# PIP-Client 应用图标

默认图标为用户选定的白灰、石墨与橙红“接收画幅”，位于 `variants/receiver`。
开放的视频画幅与两道信号图形表达图像传输和接收，不含字母、官方标志或第三方素材。
设计过程见[精修预览](../../docs/design/receiver_icon_refinement/readme.md)。

`pip_client.svg` 是 24px 及以上尺寸的图形源；`pip_client_16.svg` 是 16px 光学校正源，
加厚画框并对齐像素网格。PNG 与 ICO 使用这两份 SVG 生成，不把大图直接缩小作为 16px 图标。
运行时直接加载打包 PNG，避免依赖系统 SVG 图像插件。

新构建默认使用 `receiver`。已有 CMake 缓存需显式设置 `-DPIP_APP_ICON_VARIANT=receiver`。
该选项统一控制 Qt 窗口、Windows 可执行文件与 Ubuntu desktop/hicolor 图标。
旧 `viewlink`、`circuit`、`signal` 资源保留为设计历史，可通过该选项显式选择。

## 重新生成正式资源

普通产品构建使用已保存的 SVG、PNG、ICO，不增加 Python 或图标工具的构建依赖。
只有修改图稿后重新生成资源时，才需要 Qt 6 Gui/Svg 开发包、CMake、C++ 编译器，
以及 Python 3.9 以上和 Pillow 9.1 以上。使用与产品相同的 Qt kit，分别在 Windows/Ubuntu 构建工具：

```sh
cmake -S tools/icon_renderer -B build-icon-renderer -DCMAKE_PREFIX_PATH=<Qt安装前缀>
cmake --build build-icon-renderer
python tools/generate_app_icons.py --variant receiver --renderer <图标渲染器可执行文件>
```

可执行文件名为 `pip_icon_renderer`，Windows 带 `.exe`；多配置生成器在配置子目录中查找。
Windows 运行时需要所选 Qt kit 的运行库可被找到。资源使用 Qt SVG 四倍采样，
要求逐字节复现时固定 Qt 与 Pillow 版本；本轮使用 Qt 6.9.0、Pillow 13.0.0。
旧三套几何稿仍可通过 `python tools/generate_app_icons.py --variant circuit` 等命令重新生成。

PNG 包含 16、24、32、48、64、128、256、512 像素；Windows ICO 使用前七档透明 PNG 图层。
生成资源属于应用源资源并随源码保存。独立工具不进入产品安装包。

## 桌面身份

Ubuntu 安装同名的八档 PNG 与 scalable SVG；桌面 ID 与图标名为 `org.pip.PIPClient`。
`StartupWMClass=PIPClient` 对应既有 Qt `applicationName`，保持用户配置路径。
Windows 文件和产品版本统一来自 CMake `PROJECT_VERSION`。
资源接入和离屏检查不能代替原生桌面、DPI 与图标缓存的实际显示验收。
