pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

// RMUL 只读无数据 HUD。仅使用本地身份配置，不读取协议、视频状态或控制许可。
Item {
    id: hud

    // 已保存的 SessionSettings.robotId；0 表示未配置，不能沿用设计夹具的蓝方3号。
    required property int robotId
    // 复用宿主加载的 Barlow；中文继续使用应用已打包的 Noto Sans SC。
    required property string numberFont
    // Shader扩散只在RHI后端启用；软件后端保留文字描边，不能把降级截图当作投影验收。
    readonly property bool hasDiffuseShadows: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property int selfNumber: robotId % 100
    readonly property bool hasIdentity: robotId > 0
    readonly property string ownTeam: !hasIdentity ? "" : robotId > 100 ? "blue" : "red"
    readonly property string enemyTeam: !hasIdentity ? "" : ownTeam === "blue" ? "red" : "blue"
    // 本机诊断可用矩形，单位为宿主逻辑像素；在卡片下方、经济上方、战斗HUD左侧。
    // 只共享布局几何，诊断字号/状态由宿主管理，不从图传状态推断地图信号。
    readonly property rect diagnosticArea: Qt.rect(canvas.x + 24 * canvas.scale, canvas.y + 338 * canvas.scale, 724 * canvas.scale, Math.max(0, economy.y - 338 - 24) * canvas.scale)

    // 只映射本地配置身份；素材缺失时留中性占位，不能拿3号图标冒充5号步兵。
    function iconSource(team: string, number: int): string {
        const names = {
            1: "heavy",
            3: "infantry_3",
            4: "infantry_4",
            6: "aerial",
            7: "sentry",
            8: "dart",
            9: "radar"
        };
        return team !== "" && names[number] ? "qrc:/resources/icons/robomaster_2027/standard/" + team + "_" + names[number] + ".svg" : "";
    }

    // 名称是固定槽位/已保存身份，不表示已收到对应机器人的在线状态。
    function roleName(number: int): string {
        const names = {
            1: "重装",
            3: "3 号步兵",
            4: "4 号步兵",
            5: "5 号步兵",
            6: "空中系统",
            7: "哨兵",
            8: "飞镖",
            9: "雷达"
        };
        return names[number] || "身份未配置";
    }

    // 以下复用项均为静态图元，设计尺寸单位为2560×1440画布像素；不启动定时器或取焦点。
    // 以源图元alpha生成局部模糊色层，不给整个HUD容器加阴影，避免生成矩形底框。
    // 半径使用设计画布像素；Qt模糊核与浏览器CSS不同，参数对应意图而非声称逐像素相同。
    component DiffuseShadow: MultiEffect {
        objectName: "hudDiffuseShadow"
        required property real radius
        required property color tint
        property real verticalOffset: 0
        anchors.fill: source
        anchors.topMargin: verticalOffset
        anchors.bottomMargin: -verticalOffset
        visible: hud.hasDiffuseShadows
        blurEnabled: true
        // A方案最长22px；按8像素档扩展核上限，避免blur被限制到1后截成16px。
        blurMax: Math.max(16, Math.ceil(radius / 8) * 8)
        blur: radius / blurMax
        colorization: 1
        colorizationColor: Qt.rgba(tint.r, tint.g, tint.b, 1)
        opacity: tint.a
        autoPaddingEnabled: true
    }

    component HudText: Item {
        id: label
        property alias text: ink.text
        property alias font: ink.font
        property alias color: ink.color
        property alias horizontalAlignment: ink.horizontalAlignment
        implicitWidth: ink.implicitWidth
        implicitHeight: ink.implicitHeight
        // A基线由远到近绘制22/14px暗扩散、向下1.6px的3.2px暗影与6.4px白色衬托。
        // C方案的增强白色实验未启用；颜色采用Qt的#AARRGGBB，对应档案CSS的#RRGGBBAA。
        DiffuseShadow {
            source: ink
            radius: 22
            tint: "#5907151f"
        }
        DiffuseShadow {
            source: ink
            radius: 14
            tint: "#a607151f"
        }
        DiffuseShadow {
            source: ink
            radius: 3.2
            tint: "#e607151f"
            verticalOffset: 1.6
        }
        DiffuseShadow {
            source: ink
            radius: 6.4
            tint: "#30ffffff"
        }
        Text {
            id: ink
            anchors.fill: parent
            color: "#c0ccd5"
            font.pixelSize: 24
            style: hud.hasDiffuseShadows ? Text.Normal : Text.Outline
            styleColor: "#b307151f"
            verticalAlignment: Text.AlignVCenter
        }
    }

    component HudIcon: Item {
        id: icon
        property alias source: artwork.source
        property alias sourceSize: artwork.sourceSize
        readonly property int status: artwork.status
        // 顶部使用3.2/9.6px双层白色投影；自身大图标覆盖为4/10px及各自透明度。
        property real nearRadius: 3.2
        property real farRadius: 9.6
        property color nearTint: "#d9ffffff"
        property color farTint: "#70ffffff"
        DiffuseShadow {
            source: artwork
            radius: icon.farRadius
            tint: icon.farTint
        }
        DiffuseShadow {
            source: artwork
            radius: icon.nearRadius
            tint: icon.nearTint
        }
        Image {
            id: artwork
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
        }
    }

    component UnknownBar: Item {
        id: bar
        // 主血条两端斜切；其它未知条形保留矩形边界。斜纹不表达任何百分比。
        property bool slanted: false
        // 底部资源轨道的6px近暗影较强；主血条、占领和目标轨道使用其各自A基线透明度。
        property bool isResourceTrack: false
        DiffuseShadow {
            source: track
            radius: 16
            tint: "#7307131d"
        }
        DiffuseShadow {
            source: track
            radius: 6
            tint: bar.isResourceTrack ? "#a607131d" : "#9907131d"
        }
        Canvas {
            id: track
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.beginPath();
                ctx.moveTo(bar.slanted ? 12 : 0, 0);
                ctx.lineTo(width, 0);
                ctx.lineTo(width - (bar.slanted ? 12 : 0), height);
                ctx.lineTo(0, height);
                ctx.closePath();
                ctx.clip();
                ctx.fillStyle = "#590a1727";
                ctx.fillRect(0, 0, width, height);
                ctx.strokeStyle = "#13a8bac0";
                ctx.lineWidth = 3;
                for (let x = -height; x < width + height; x += 12) {
                    ctx.beginPath();
                    ctx.moveTo(x, height);
                    ctx.lineTo(x + height * 0.577, 0);
                    ctx.stroke();
                }
            }
        }
    }

    component RobotCard: Item {
        id: card
        required property int number
        required property string team
        required property bool isSelf
        width: 156
        height: 204
        scale: isSelf ? 1.2 : 1
        transformOrigin: Item.Top
        Rectangle {
            anchors.fill: parent
            color: "#800e1d29"
        }
        // 渐变只画条外描边，不添加血量填充；本人强调不依赖遥测到达。
        DiffuseShadow {
            source: outline
            radius: 16
            tint: card.team === "blue" ? "#38246ccb" : "#38d63232"
            visible: card.isSelf && hud.hasDiffuseShadows
        }
        DiffuseShadow {
            source: outline
            radius: 10
            tint: "#8007131d"
            verticalOffset: 3
            visible: card.isSelf && hud.hasDiffuseShadows
        }
        Canvas {
            id: outline
            x: -4
            y: -4
            width: card.width + 8
            height: card.height + 8
            visible: card.isSelf
            onVisibleChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const gradient = ctx.createLinearGradient(0, 0, width, height);
                gradient.addColorStop(0, "#e0f7fcff");
                gradient.addColorStop(0.38, card.team === "blue" ? "#a7c4ea" : "#eba3a3");
                gradient.addColorStop(0.72, card.team === "blue" ? "#b8246ccb" : "#b8d63232");
                gradient.addColorStop(1, "#adf7fcff");
                ctx.strokeStyle = gradient;
                ctx.lineWidth = 2;
                ctx.strokeRect(1, 1, width - 2, height - 2);
            }
            // 同编号跨阵营切换会复用delegate，描边颜色必须同步重绘。
            Connections {
                target: card
                function onTeamChanged() {
                    outline.requestPaint();
                }
            }
        }
        HudIcon {
            objectName: card.objectName + "_symbol"
            x: 13
            y: 3
            width: 60
            height: 60
            source: hud.iconSource(card.team, card.number)
            sourceSize: Qt.size(128, 128)
            opacity: 0.8
        }
        HudText {
            x: 13
            width: 60
            height: 66
            text: "?"
            font.family: hud.numberFont
            font.pixelSize: 42
            horizontalAlignment: Text.AlignHCenter
            visible: card.team === ""
        }
        HudText {
            objectName: card.objectName + "_name"
            x: 10
            y: 76
            height: 24
            text: hud.roleName(card.number)
            font.pixelSize: 20
        }
        Row {
            x: 10
            y: 109
            height: 33
            spacing: 4
            // 当前值/上限分别使用33/16px；二者均未知，不能用零值代替未接入。
            HudText {
                objectName: card.objectName + "_hp"
                height: 33
                text: "—"
                font.family: hud.numberFont
                font.bold: true
                font.pixelSize: 33
                color: "#d7e1e6"
            }
            HudText {
                objectName: card.objectName + "_maximum"
                y: 17
                height: 16
                text: "/ —"
                font.family: hud.numberFont
                font.pixelSize: 16
                color: "#d7e1e6"
            }
        }
        HudText {
            x: 10
            y: 164
            height: 16
            text: "AMMO"
            font.family: hud.numberFont
            font.pixelSize: 16
        }
        HudText {
            objectName: card.objectName + "_ammo"
            x: 96
            y: 156
            width: 50
            height: 24
            text: "—"
            font.family: hud.numberFont
            font.pixelSize: 24
            horizontalAlignment: Text.AlignRight
        }
    }

    // 本轮只有无数据状态：地点身份已知，归属/激活/计数未知，不能画中立实线或播放夺点动画。
    component UnknownControlPoint: Item {
        id: point
        // 官方固定地点字母B/C/R，随视角重排，不能随己敌关系重命名。
        required property string location
        // 未接入归属来源；不能根据地点、RFID或本机墙钟推断控制方。
        readonly property string owner: "unknown"
        // 虚线与已确认中立的实线区分；该只读标志与本组件固定绘图语义一致。
        readonly property bool hasDashedBorder: true
        width: 44
        height: 44
        Accessible.role: Accessible.StaticText
        Accessible.name: location === "C" ? "中央控制区" : location + "高地控制区"
        Accessible.description: "控制归属、激活和占领进度均未接入"
        DiffuseShadow {
            source: unknownOutline
            radius: 5
            tint: "#cc07151f"
        }
        Item {
            id: unknownOutline
            x: 5
            y: 5
            width: 34
            height: 34
            // 固定虚线用场景图矩形绘制，避免阵营切换重建delegate后Canvas首帧尚未生成纹理。
            // 每条边4px实线/3px间隔、3px厚；旋转只用于四条静态边，字母和点形状不旋转。
            Repeater {
                model: 4
                Item {
                    required property int index
                    width: 34
                    height: 34
                    rotation: index * 90
                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            x: index * 7
                            width: 4
                            height: 3
                            color: "#c1cbcf"
                        }
                    }
                }
            }
        }
        HudText {
            anchors.fill: parent
            text: point.location
            font.family: hud.numberFont
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
        }
    }

    component ResourceRow: Item {
        id: resource
        required property string label
        required property string unit
        height: 32
        HudText {
            width: 53
            height: parent.height
            text: resource.label
            font.pixelSize: 21
        }
        UnknownBar {
            objectName: "hudResourceTrack"
            isResourceTrack: true
            x: 63
            width: resource.width - 152
            height: 11
            anchors.verticalCenter: parent.verticalCenter
        }
        HudText {
            anchors.right: parent.right
            width: 79
            height: parent.height
            text: "— " + resource.unit
            font.family: hud.numberFont
            font.pixelSize: 25
            horizontalAlignment: Text.AlignRight
        }
    }

    // 设计画布按窗口可用空间等比缩放并居中，与视频contentRect、帧率和黑边完全独立。
    // Qt负责系统DPI；菜单字体档位不再二次放大HUD，以避免最小窗口裁切固定构图。
    Item {
        id: canvas
        objectName: "hudCanvas"
        width: 2560
        height: 1440
        scale: Math.min(hud.width / width, hud.height / height)
        x: (hud.width - width * scale) / 2
        y: (hud.height - height * scale) / 2
        transformOrigin: Item.TopLeft

        // 上下区域共用48设计像素安全边距；本人上移7px后的1.2倍描边/16px投影仍有留白。
        // 边距随画布等比缩放，窗口和全屏共用，不能依赖标题栏或非16:9黑边提供空间。
        readonly property real edgeInset: 48

        Item {
            objectName: "hudTopTeams"
            x: 53.3333
            y: canvas.edgeInset
            width: 2453.3334
            height: 270
            Repeater {
                model: [7, 3, 1]
                RobotCard {
                    required property int index
                    required property int modelData
                    objectName: "hudOwnCard_" + number
                    x: index * 188
                    y: isSelf ? -7 : 0
                    number: modelData
                    team: hud.ownTeam
                    isSelf: hud.hasIdentity && number === hud.selfNumber
                }
            }
            Repeater {
                model: [1, 3, 7]
                RobotCard {
                    required property int index
                    required property int modelData
                    objectName: "hudEnemyCard_" + number
                    x: parent.width - 532 + index * 188
                    number: modelData
                    team: hud.enemyTeam
                    isSelf: false
                }
            }
        }
        Item {
            x: 830
            // 中央组沿用新版43.3333px顶距；卡片单独保留48px安全边距，计时与卡片无需同顶。
            y: 43.3333
            width: 900
            height: 224
            Item {
                id: scoreboard
                objectName: "hudScoreboard"
                width: 900
                height: 42
                // 全队控制度与逐点0–5计数不同。无数据时两侧只画未知轨道和横杠，无600/夹具填充。
                UnknownBar {
                    objectName: "hudOwnControlTrack"
                    width: 373
                    height: 42
                }
                HudText {
                    objectName: "hudOwnControlValue"
                    x: 12
                    width: 349
                    height: 42
                    text: "—"
                    font.family: hud.numberFont
                    font.pixelSize: 36
                    font.letterSpacing: 2
                    horizontalAlignment: Text.AlignRight
                }
                Rectangle {
                    x: 377
                    width: 146
                    height: 42
                    color: "#26b7c2c2"
                }
                HudText {
                    objectName: "hudMatchTime"
                    x: 377
                    width: 146
                    height: 42
                    text: "—:—"
                    font.family: hud.numberFont
                    font.pixelSize: 40
                    font.letterSpacing: 2
                    horizontalAlignment: Text.AlignHCenter
                }
                UnknownBar {
                    objectName: "hudEnemyControlTrack"
                    x: 527
                    width: 373
                    height: 42
                }
                HudText {
                    objectName: "hudEnemyControlValue"
                    x: 539
                    width: 349
                    height: 42
                    text: "—"
                    font.family: hud.numberFont
                    font.pixelSize: 36
                    font.letterSpacing: 2
                    horizontalAlignment: Text.AlignLeft
                }
            }
            Row {
                objectName: "hudControlPoints"
                anchors.horizontalCenter: parent.horizontalCenter
                y: scoreboard.height + 28
                height: 44
                spacing: 16
                Repeater {
                    // 未配置身份时只保留中性参考顺序；字母不代表当前归属。
                    model: hud.ownTeam === "red" ? ["R", "C", "B"] : ["B", "C", "R"]
                    UnknownControlPoint {
                        required property int index
                        required property string modelData
                        objectName: "hudControlPoint_" + index
                        location: modelData
                    }
                }
            }
            HudText {
                objectName: "hudDataNotice"
                x: 240
                y: 196
                width: 420
                height: 28
                text: "等待比赛数据"
                font.pixelSize: 19
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // 建筑省略后仍保留独立的敌方经济，数据未知且不从其它字段推导。
        HudText {
            objectName: "hudEnemyEconomy"
            x: 2032
            y: 394
            width: 506.6667
            height: 32
            text: "当前经济  —    经济总量  —"
            font.pixelSize: 19
            horizontalAlignment: Text.AlignRight
        }
        Item {
            id: minimap
            objectName: "hudMinimap"
            x: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            width: 324
            height: 484
            Accessible.role: Accessible.Graphic
            Accessible.name: "RMUL场地小地图"
            Accessible.description: hud.hasIdentity ? "静态场地，己方在下；位置数据未接入，不绘制哨兵" : "阵营未配置，中性参考方向；位置数据未接入"
            DiffuseShadow {
                source: mapFrame
                radius: 19.2
                tint: "#4008131d"
                verticalOffset: -3.2
            }
            Rectangle {
                id: mapFrame
                anchors.fill: parent
                color: "#414141"
                border.width: 2
                border.color: "#ccadb5ba"
            }
            // 产品资源已删除哨兵、圆底、方向和射击夹具；未知身份不得默认蓝方或红方。
            // 红蓝地形相差180度，符号正立；不在QML旋转整张图。
            Image {
                objectName: "hudMinimapTerrain"
                x: 2
                y: 2
                width: 320
                height: 480
                source: "qrc:/resources/maps/rmul/silver_" + (hud.hasIdentity ? hud.ownTeam : "neutral") + ".svg"
                sourceSize: Qt.size(Math.max(1, Math.ceil(width * canvas.scale * Screen.devicePixelRatio)), Math.max(1, Math.ceil(height * canvas.scale * Screen.devicePixelRatio)))
            }
        }
        Item {
            id: economy
            objectName: "hudOwnEconomy"
            x: minimap.x
            y: minimap.y - height - 20
            width: minimap.width
            height: 132
            // 三行经济与地图等宽；金币仍未知，不复制夹具的有效数值或暖金强调。
            HudText {
                y: 12
                height: 28
                text: "己方经济"
                font.pixelSize: 20
            }
            Row {
                anchors.right: parent.right
                spacing: 6
                HudText {
                    height: 40
                    text: "—"
                    font.family: hud.numberFont
                    font.pixelSize: 40
                    font.bold: true
                }
                HudText {
                    y: 16
                    height: 24
                    text: "金币"
                    font.pixelSize: 18
                }
            }
            HudText {
                y: 48
                height: 34
                text: "累计获得"
                font.pixelSize: 20
            }
            HudText {
                y: 48
                width: parent.width
                height: 34
                text: "—"
                font.family: hud.numberFont
                font.pixelSize: 26
                horizontalAlignment: Text.AlignRight
            }
            HudText {
                y: 90
                height: 34
                text: "可兑换弹量"
                font.pixelSize: 20
            }
            HudText {
                y: 90
                width: parent.width
                height: 34
                text: "— 发"
                font.pixelSize: 26
                horizontalAlignment: Text.AlignRight
            }
        }
        Item {
            objectName: "hudCombat"
            x: 796
            y: 524
            width: 830.6667
            height: 430
            Item {
                y: 24
                width: 68
                height: 370
                HudText {
                    width: 68
                    height: 32
                    text: "AMMO"
                    font.family: hud.numberFont
                    font.pixelSize: 23
                    horizontalAlignment: Text.AlignHCenter
                }
                Column {
                    x: 16.6667
                    y: 39
                    spacing: 4
                    Repeater {
                        model: 20
                        Rectangle {
                            width: 34.6667
                            height: 10.1333
                            color: "#26c4d4de"
                        }
                    }
                }
                HudText {
                    y: 326
                    width: 68
                    height: 52
                    text: "—"
                    font.family: hud.numberFont
                    font.pixelSize: 46
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            // 静态虚线双圆弧共用未知状态；无亮进度、无安全/过热结论。
            DiffuseShadow {
                source: heatStroke
                radius: 6.4
                tint: "#b308131d"
            }
            Canvas {
                id: heatStroke
                x: 137.3333
                width: 693.3333
                height: 426.6667
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = "#5cb7c8d1";
                    ctx.lineWidth = 14.6667;
                    // Qt Canvas 的虚线长度以线宽为单位；对应设计SVG中的4/5单位虚实间隔。
                    ctx.setLineDash([4 / 11, 5 / 11]);
                    for (const angle of [-35, 145]) {
                        ctx.beginPath();
                        ctx.arc(346.6667, 213.3333, 320, angle * Math.PI / 180, (angle + 70) * Math.PI / 180);
                        ctx.stroke();
                    }
                }
            }
            DiffuseShadow {
                source: crosshair
                radius: 1.6
                tint: "#ff071018"
            }
            DiffuseShadow {
                source: crosshair
                radius: 1.6
                tint: "#99071018"
                verticalOffset: 1.6
            }
            Item {
                id: crosshair
                objectName: "hudCrosshair"
                x: 456
                y: 185.3333
                width: 56
                height: 56
                Rectangle {
                    x: 0
                    y: 27.3333
                    width: 12
                    height: 1.3333
                    color: "#effbff"
                }
                Rectangle {
                    x: 44
                    y: 27.3333
                    width: 12
                    height: 1.3333
                    color: "#effbff"
                }
                Rectangle {
                    x: 27.3333
                    y: 0
                    width: 1.3333
                    height: 12
                    color: "#effbff"
                }
                Rectangle {
                    x: 27.3333
                    y: 44
                    width: 1.3333
                    height: 12
                    color: "#effbff"
                }
                Rectangle {
                    x: 26
                    y: 26
                    width: 4
                    height: 4
                    color: "#effbff"
                }
            }
            HudText {
                objectName: "hudHeatValue"
                x: 137.3333
                y: 388
                width: 693.3333
                height: 42
                text: "热量  — %"
                font.pixelSize: 25
                horizontalAlignment: Text.AlignHCenter
            }
        }
        Item {
            objectName: "hudTarget"
            x: 1709.3333
            y: 557.3333
            width: 288
            height: 284
            HudText {
                width: 72
                height: 72
                text: "?"
                font.family: hud.numberFont
                font.pixelSize: 48
                horizontalAlignment: Text.AlignHCenter
            }
            HudText {
                objectName: "hudTargetName"
                x: 84
                width: 204
                height: 42
                text: "目标未知"
                font.pixelSize: 32
            }
            HudText {
                x: 84
                y: 43
                height: 29
                text: "未接入"
                font.pixelSize: 23
            }
            Repeater {
                model: ["HP", "AMMO", "HEAT"]
                Item {
                    required property int index
                    required property string modelData
                    y: 91 + index * 66
                    width: 288
                    height: 53
                    HudText {
                        height: 38
                        text: parent.modelData
                        font.family: hud.numberFont
                        font.pixelSize: 23
                    }
                    HudText {
                        width: 288
                        height: 38
                        text: "—"
                        font.family: hud.numberFont
                        font.pixelSize: 33
                        horizontalAlignment: Text.AlignRight
                    }
                    UnknownBar {
                        objectName: "hudTargetTrack_" + parent.index
                        y: 40
                        width: 288
                        height: 9
                    }
                }
            }
        }
        Item {
            objectName: "hudDashboard"
            x: 700
            anchors.bottom: parent.bottom
            anchors.bottomMargin: canvas.edgeInset
            width: 1160
            height: 224
            HudIcon {
                objectName: "hudSelfIcon"
                nearRadius: 4
                farRadius: 10
                nearTint: "#b3ffffff"
                farTint: "#4dffffff"
                x: 8
                y: 5
                width: 160
                height: 160
                source: hud.iconSource(hud.ownTeam, hud.selfNumber)
                sourceSize: Qt.size(256, 256)
            }
            HudText {
                x: 8
                y: 5
                width: 160
                height: 160
                text: "?"
                font.family: hud.numberFont
                font.pixelSize: 84
                horizontalAlignment: Text.AlignHCenter
                visible: hud.iconSource(hud.ownTeam, hud.selfNumber) === ""
            }
            HudText {
                y: 170
                width: 176
                height: 32
                text: hud.hasIdentity ? hud.roleName(hud.selfNumber) : "身份未配置"
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
            }
            Item {
                x: 202
                width: 958
                height: 224
                UnknownBar {
                    objectName: "hudHpTrack"
                    width: 958
                    height: 57
                    slanted: true
                }
                HudText {
                    objectName: "hudSelfHp"
                    width: 958
                    height: 57
                    text: "— / —"
                    font.family: hud.numberFont
                    font.pixelSize: 36
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
                HudText {
                    objectName: "hudCombatStatus"
                    x: 972
                    height: 57
                    text: "脱战未知"
                    font.pixelSize: 22
                }
                ResourceRow {
                    objectName: "hudCapacitorRow"
                    y: 72
                    width: 958
                    label: "超电"
                    unit: "%"
                }
                ResourceRow {
                    objectName: "hudBufferRow"
                    y: 117
                    width: 466
                    label: "缓冲"
                    unit: "J"
                }
                ResourceRow {
                    objectName: "hudPowerRow"
                    x: 492
                    y: 117
                    width: 466
                    label: "功率"
                    unit: "W"
                }
                Flow {
                    y: 167
                    width: 958
                    spacing: 12
                    Repeater {
                        model: ["主控", "电源管理", "图传", "RFID", "UWB", "装甲板", "小弹丸", "大弹丸", "超级电容", "灯条"]
                        Row {
                            id: moduleStatus
                            required property string modelData
                            spacing: 3
                            Accessible.role: Accessible.StaticText
                            Accessible.name: modelData
                            Accessible.description: "未收到模块状态数据，红灯表示不可用，未诊断硬件故障"
                            // 当前只做无数据前端，红灯保留官方PNG原光晕，再叠加A基线5px暗影。
                            // 不根据图传、MQTT或设备连接状态推断模块在线；尺寸为设计画布像素。
                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20
                                height: 22
                                // 投影放在固定灯槽内，不能作为Row的额外布局项挤开名称。
                                DiffuseShadow {
                                    source: moduleLamp
                                    radius: 5
                                    tint: "#b307131d"
                                }
                                Image {
                                    id: moduleLamp
                                    objectName: "hudModuleLamp_" + moduleStatus.modelData
                                    anchors.fill: parent
                                    source: "qrc:/resources/icons/module_lamps/statusbar_signallamp_red_single.png"
                                    fillMode: Image.PreserveAspectFit
                                }
                            }
                            HudText {
                                objectName: "hudModuleLabel_" + moduleStatus.modelData
                                height: 27
                                text: moduleStatus.modelData
                                font.pixelSize: 18
                            }
                        }
                    }
                }
            }
        }
        Column {
            objectName: "hudKeyHints"
            x: 2192
            anchors.bottom: parent.bottom
            anchors.bottomMargin: canvas.edgeInset
            width: 368
            // 使用内容高度锚定，避免固定空槽让最后一行的实际底边偏离状态栏。
            height: implicitHeight
            spacing: 10.6667
            // 只绘制参考中的提示；本组件没有鼠标/键盘处理器，也不下发控制或新增快捷动作。
            Repeater {
                model: [
                    {
                        keys: ["W", "A", "S", "D"],
                        label: "移动"
                    },
                    {
                        keys: ["Shift"],
                        label: "加速",
                        extraKey: "R",
                        extraLabel: "补给"
                    },
                    {
                        keys: ["Tab"],
                        label: "战场信息"
                    },
                    {
                        keys: ["Esc"],
                        label: "菜单"
                    }
                ]
                Row {
                    id: hint
                    required property var modelData
                    spacing: 12
                    Row {
                        width: Math.max(84, implicitWidth)
                        spacing: 4
                        Repeater {
                            model: hint.modelData.keys
                            Rectangle {
                                required property string modelData
                                width: Math.max(36, keyText.implicitWidth + 16)
                                height: 37.3333
                                color: "#24edf5f4"
                                HudText {
                                    id: keyText
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    font.family: hud.numberFont
                                    font.pixelSize: 23
                                }
                            }
                        }
                    }
                    HudText {
                        height: 37.3333
                        text: hint.modelData.label
                        font.pixelSize: 24
                    }
                    Rectangle {
                        visible: !!hint.modelData.extraKey
                        width: 36
                        height: 37.3333
                        color: "#24edf5f4"
                        HudText {
                            anchors.centerIn: parent
                            text: hint.modelData.extraKey || ""
                            font.family: hud.numberFont
                            font.pixelSize: 23
                        }
                    }
                    HudText {
                        visible: !!hint.modelData.extraLabel
                        height: 37.3333
                        text: hint.modelData.extraLabel || ""
                        font.pixelSize: 24
                    }
                }
            }
        }
    }
}
