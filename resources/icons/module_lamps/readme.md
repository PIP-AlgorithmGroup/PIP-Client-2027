# RMUL 模块状态灯

`statusbar_signallamp_red_single.png`直接复制自
`docs/design/custom_terminal/assets/module_lamps/`，保留官方透明PNG与光晕，未重绘或转码。

- 来源：`RoboMasterClient 12.0.0.213_Student`，`RoboMasterClient_Data/resources.assets`。
- 对象：`statusbar_signallamp_red_single`，path_id为1070，原尺寸63×64。
- SHA-256：`f012474f7638cfbccaa3f019c070fb64ed5906920bd7338c12a7b1aeb423bcc1`。
- 原包版本、下载出处和提取证据见[设计档案](../../../docs/design/custom_terminal/readme.md)
  与[素材清单](../../../docs/design/custom_terminal/asset_manifest.json)。权利归原权利人。

正式RMUL目前只做无数据前端，按2026-11-21设计使用红灯表示未收到模块状态，
不单独推断硬件故障。显示20×22设计像素、与名称间隔3像素，不显示尾随问号。
在线绿灯和确认不适用的灰灯规则仍由设计档案保存，待业务状态接入后使用；本次不打包未使用资源。
