# RMUL Silver静态场地

来自[Silver设计规范](../../../docs/design/custom_terminal/minimap_design.md)的正式产品子集。
地形依据2027联盟赛V1.0.0（20261015）规则手册图3-5、图3-8，属于项目示意图，
不是官方导航地图，不可用于测量或判断RFID边界。固定双剑/维修符号提取自规则手册原嵌入图，
权利归原权利人，原始来源与处理记录见设计档案的`assets/minimap/source_manifest.json`。

- `silver_blue.svg`、`silver_red.svg`保留各自己下敌上的视角，场地和坐标相差180度，固定符号正立。
- `silver_neutral.svg`在身份未配置时使用中性参考方向，两个维修符号去色，不暗示默认蓝方或红方。
- 三张产品图均不包含哨兵、圆底、云台箭头和交火夹具；未知位置不画动态标记或盖图文字。
- 地形内嵌SVG展开为矢量组，PNG使用Qt兼容的xlink引用，全部内容独立离线加载。

运行`python tools/generate_rmul_minimap.py`可从设计源SVG重新导出；需要现有图标工具同样使用的Pillow。
产品构建只读取已保存SVG，不依赖Python、Pillow、设计HTML或临时目录。
`manifest.json`保存源图和产品图的SHA-256，可核对设计改动与导出漂移。
