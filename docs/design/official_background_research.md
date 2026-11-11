# RoboMaster 官方背景素材核查

核查日期：2026-11-11。以下尺寸来自图片解码、浏览器原生媒体尺寸或视频流探测，不以文件名中的
`large`、`ld` 或网页显示尺寸作为判断依据。没有修改现有背景或应用代码。

## 已核实的官方高清素材

### 优先推荐：官方赛场插画与动态主视觉

[组委会品牌宣传包页面](https://bbs.robomaster.com/wiki/20204847/810999?source=7)
在 2026-10-15 发布的素材资料包入口指向
[RM2027电子宣传礼包](https://pan-sec.djicorp.com/s/jseHa7iiNGqWGEi)。
当前礼包仍包含 2025、2026 年主视觉，不能将礼包名称当成所有素材的赛季。
进入 `06.赛季赛事kv`，可以查看下表文件。

| 素材 | 实际规格 | 画面与判断 |
| --- | --- | --- |
| 26全国赛 / RoboMaster 2026全国赛KV-纯净版png.png | 5760 × 3240；页面标注 23.1 MB | 金色巨型 R、未来城市、环形赛场、机器人；天空明亮，最接近当前游戏欢迎页方向，但与原图构图不同 |
| 25 全国赛 / RM全国赛kv-纯净版.png | 5760 × 3240；页面标注 19.8 MB | 体育场、金色光线、机器人，中央包含 THE FINALS 视觉标识；“纯净版”不等于完全无字 |
| 25 全国赛 / RMUC2025 全国总决赛动态KV.mp4 | 浏览器 videoWidth/videoHeight 为 3840 × 2160，duration 为 8 秒；页面标注 57.9 MB | 官方动态主视觉，包含总决赛标识及左右大字口号，容易与菜单争夺注意力；是否无缝循环仍需播放验收 |
| 25 区域赛 / 25 区域赛-横版无字.jpg | 3840 × 2160；页面标注 1.8 MB | 暖橙色未来城市、金拳奖杯、机器人与人物，与当前图同属赛场概念插画，色调不同 |
| 26区域赛 / 2026 robomaster 区域赛纯净版.png | 5760 × 3240；页面标注 20.1 MB | 橙色 ROBOMASTER 大字、人物和机器人，文字很强，不优先作为菜单背景 |

静态图尺寸由官方文件预览中的 `naturalWidth` / `naturalHeight` 核实，
动态尺寸由实际加载的视频元素核实；均非 CSS 显示尺寸。
图库已经提供真正高清的原素材，无需通过 AI 重建这些候选。
另已下载 2026 全国赛 PNG 并解码确认 5760 × 3240、24,178,841 字节；
2025 动态 KV 经 ffprobe 确认为 H.264、60 fps、8.000 秒、60,705,681 字节，带 AAC 音轨。
2026 纯净版仍包含场景内的赛季环绕文字，适用于该视觉的评审，不应据此冒充 2027 主视觉。

原文件链接：

- [2026 全国赛纯净版 PNG](https://pan-link.djicorp.com/s/jseHa7iiNGqWGEi/download?path=/06.%E8%B5%9B%E5%AD%A3%E8%B5%9B%E4%BA%8Bkv/26%E5%85%A8%E5%9B%BD%E8%B5%9B/&files=RoboMaster%202026%E5%85%A8%E5%9B%BD%E8%B5%9BKV-%E7%BA%AF%E5%87%80%E7%89%88png.png)
- [2025 全国总决赛动态 KV MP4](https://pan-link.djicorp.com/s/jseHa7iiNGqWGEi/download?path=/06.%E8%B5%9B%E5%AD%A3%E8%B5%9B%E4%BA%8Bkv/25%20%E5%85%A8%E5%9B%BD%E8%B5%9B/&files=RMUC2025%20%E5%85%A8%E5%9B%BD%E6%80%BB%E5%86%B3%E8%B5%9B%E5%8A%A8%E6%80%81KV.mp4)
- [2025 区域赛横版无字 JPG](https://pan-link.djicorp.com/s/jseHa7iiNGqWGEi/download?path=/06.%E8%B5%9B%E5%AD%A3%E8%B5%9B%E4%BA%8Bkv/25%20%E5%8C%BA%E5%9F%9F%E8%B5%9B/&files=25%20%E5%8C%BA%E5%9F%9F%E8%B5%9B-%E6%A8%AA%E7%89%88%E6%97%A0%E5%AD%97.jpg)

### 其他备选：实拍素材

| 候选 | 实际规格 | 画面与适用性 | 官方来源 |
| --- | --- | --- | --- |
| 官网首页循环视频 | 1920 × 1080；H.264；25 fps；20.009 秒；16,358,222 字节；另带 AAC 音轨 | 赛事现场实拍剪辑，包含场馆旗帜；不是蓝天未来城市插图的动画版本。官网以 autoplay、loop、muted 播放 | [官网首页](https://www.robomaster.com/zh-CN) / [MP4 原始地址](https://vod.robomaster.com/7044fd3699ee71f180776723a78f0102/cd8b8d1d7ac548bbb164d9cae725676b-46fee4569b9077498bbcaeb99ff668ed-ld.mp4) |
| 首页视频封面 | 4000 × 2667 JPEG；753,907 字节 | 金色拳头奖杯近景实拍，有黄色光束和虚化背景；主体清楚，但画风和构图不同于现有插图 | [官网首页](https://www.robomaster.com/zh-CN) / [JPEG 原图](https://rm-static.djicdn.com/documents/156026/3a6abe59ed2521787813782561764809.jpg) |
| 官方旧图片资料页：赛场机器人 | 1920 × 1280 JPEG；436,231 字节 | 明亮的实际赛场、蓝色机器人；没有未来城市和巨型金拳奖杯 | [官方图片资料页](https://www.robomaster.com/zh-CN/resource/image) / [JPEG 原图](https://rm-static.djicdn.com/robomasters/public/img/image-img04-large.d33c2db.jpg) |
| 官方旧图片资料页：队伍旗帜 | 1920 × 1280 JPEG；498,763 字节 | 赛事集体实拍，人物与旗帜较多，不适合直接替代当前插画 | [官方图片资料页](https://www.robomaster.com/zh-CN/resource/image) / [JPEG 原图](https://rm-static.djicdn.com/robomasters/public/img/image-img01-large.243db03.jpg) |

首页 HTML 的 `video` 元素直接给出上述 MP4 与 poster URL；视频也明确设置了
`autoplay`、`loop`、`muted`。实际规格通过 ffprobe 读取。官网图片资料页在其
[公开页面脚本](https://rm-static.djicdn.com/robomasters/public/pages/_lang/resource/image.3f928fd80180c2241465.js)
中区分预览图与 `main_img_url` 大图；实际解码发现部分 `large` 文件只有约 477 × 358，
因此不能仅凭该名称判定高清。

## 与当前背景同构图的高清版本

当前使用的[官方原图](https://rm-static.djicdn.com/documents/68659/9eb66bab6cafb1723811240976291454.png)
仍为 800 × 400，出现在[官方规则/资料中心](https://bbs.robomaster.com/wiki/20204847)
页面数据的 `headImg` 字段。已核查的首页主视觉与旧图片资料页没有发现同构图的高清原版或动画。
这只能说明本次已检查入口没有找到，不能证明官方从未发布过。

随后通过资料中心界面进入品牌宣传包，核实了上述五份官方主视觉。
这五份均不是当前蓝天金拳插图的同构图高清原版；它们提供了可替换的高清视觉方向。

## 使用判断

建议优先评审 2026 全国赛纯净版静态图和 2025 全国总决赛动态 KV，
保留现有布局方向，按新背景主体位置调整局部可读性。尚未替换欢迎页样稿或 QML。

礼包根目录明确说明：内容仅可用于战队宣传，禁止商业化使用，禁止对品牌标识做变形或变色处理。
客户端发布分发是否适用需另行确认；本次仅作设计评审，没有加入应用发布包。
