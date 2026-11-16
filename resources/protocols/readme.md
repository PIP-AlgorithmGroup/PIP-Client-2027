# 原生输入协议资源

Wayland XML 固定来自 wayland-protocols 1.31：

- [relative-pointer-unstable-v1.xml](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.31/unstable/relative-pointer/relative-pointer-unstable-v1.xml)
- [pointer-constraints-unstable-v1.xml](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.31/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml)

两份文件保留上游版权和许可正文。Ubuntu 构建使用系统 `wayland-scanner` 离线生成客户端头和实现，
运行时依赖系统 `libwayland-client`、`libX11`、`libXi`；Windows 使用系统 Raw Input API。
Qt QPA 原生 surface 接口依赖项目固定的 Qt 6.12.0 ABI；不接管 Qt 自有 proxy 的队列或所有权。
