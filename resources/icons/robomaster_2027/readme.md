# RoboMaster 2027 已选定图标

本目录固定保存用户选定的第五版素材：大图保留机器人造型及编号角标，
小地图的重装、两台步兵使用独立大号 1/3/4，其余兵种保留符号。

- `standard/`：14 个大图标 SVG，直接来自选定包。
- `compact/`：14 个小地图 SVG，直接来自选定包。
- `png/standard/`、`png/compact/`：24/32/48/64/128/256 px，共 168 张透明 PNG。
- [浅色预览](preview_light.png)、[深色预览](preview_dark.png)、[实际尺寸检查](size_check.png)。
- [用户选定的原始素材包](robomaster_corrected_v5.zip)：逐字节保留，不重新打包。
- [图形来源清单](manifest.json)、[官方参考资料目录](../../../docs/references/robomaster/readme.md)。
- [原素材和提取图案](../../../docs/references/robomaster/materials/icon_sources)。

`standard`/`compact` 表示用途，不以 PNG 分辨率区分图形。大图的 24 px 导出仍保留机器人造型。
64×64 viewBox，无字体和外部文件依赖。红 #D63232、蓝 #246CCB 为项目屏幕方案色。
空中、哨兵、飞镖有官方客户端资源依据；雷达符号仍只有历史二手配图，官方源稿未取得。
官方原素材的权利归各自权利人，本目录不声明整套图标获得官方认证。

本轮仅保存素材，没有将其加入 QRC、CMake、安装列表或界面逻辑。后续接入从本目录引用，
原始参考资料不进入应用运行时。设计记录见[设计说明](../../../docs/design/icons/robomaster_2027/readme.md)。
