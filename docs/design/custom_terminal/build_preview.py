"""从唯一可编辑片段更新已归档的离线浏览包装及样式快照，只使用 Python 标准库。"""

import html
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def minimap_assets(root):
    """从自包含Silver源SVG分离静态地形与己方哨兵；保留原坐标及所有透明素材。"""
    ET.register_namespace("", "http://www.w3.org/2000/svg")
    result = {}
    for team in ("blue", "red"):
        svg = ET.fromstring((root / f"assets/minimap/silver_{team}.svg").read_text(encoding="utf-8"))
        markers = [node for node in svg if node.get("data-robot")]
        if len(markers) != 1 or markers[0].get("data-robot") != f"{team}-sentry":
            raise ValueError(f"Silver map must contain exactly one allied sentry: {team}")
        svg.remove(markers[0])
        result[team] = {"terrain": ET.tostring(svg, encoding="unicode"),
                        "marker": ET.tostring(markers[0], encoding="unicode")}
    return result


def main():
    """保留独立浏览器运行外壳，只替换标记内的视觉稿；不连接或执行产品代码。"""
    root = Path(__file__).resolve().parent
    fragment = (root / "preview_fragment.html").read_text(encoding="utf-8")
    # 仅同步资源常量，展示行为继续由唯一HUD源稿维护；不依赖会话文件路径。
    fragment, replacements = re.subn(
        r"    const minimap_assets = .*?;\n",
        lambda match: "    const minimap_assets = " + json.dumps(minimap_assets(root)) + ";\n",
        fragment, count=1)
    if replacements != 1:
        raise ValueError("Silver minimap asset binding is missing")
    (root / "preview_fragment.html").write_text(fragment, encoding="utf-8", newline="\n")
    index_path = root / "index.html"
    document = index_path.read_text(encoding="utf-8")
    attribute = re.search(r'data-srcdoc="([^"]*)"', document)
    if attribute is None:
        raise ValueError("Offline preview iframe is missing")
    content = html.unescape(attribute.group(1))
    start = "<!-- __TERMINAL_DESIGN_START__ -->"
    end = "<!-- __TERMINAL_DESIGN_END__ -->"
    replacement = start + "\n" + fragment + end
    if start in content and end in content:
        content = re.sub(re.escape(start) + ".*?" + re.escape(end), lambda match: replacement,
                         content, count=1, flags=re.S)
    elif fragment in content:
        content = content.replace(fragment, replacement, 1)
    else:
        raise ValueError("Cannot locate the source fragment in the offline shell")
    # 此视觉稿使用内嵌官方/项目图标，导出壳自带的远程Lucide脚本没有用途，移除网络依赖。
    content = re.sub(r'<script id="codex-visualization-lucide"[^>]*>\s*</script>', "", content)
    # 通用导出壳的small/字体重置会改动局部字形位置；视觉稿有完整自有样式，清除该层干扰。
    content = re.sub(r"<style>.*?html>body\{padding:0\}</style>",
                     "<style>html,body{margin:0;padding:0}</style>", content, count=1, flags=re.S)
    content = content.replace('<html lang="en"', '<html lang="zh-CN"')
    document = document[:attribute.start(1)] + html.escape(content, quote=True) + document[attribute.end(1):]
    # 外层边距属于导出壳，不属于 2K 画布；移除它以便屏幕截图与设计坐标一致。
    document = document.replace("padding:1rem;background:inherit", "padding:0;background:inherit")
    document = document.replace("height:calc(100vh - 2rem)", "height:100vh")
    index_path.write_text(document, encoding="utf-8", newline="\n")
    style = re.search(r"<style>(.*?)</style>", fragment, re.S)
    if style is None:
        raise ValueError("Source stylesheet is missing")
    font_index = iter(range(2))
    readable_style = re.sub(r'data:font/ttf;base64,[A-Za-z0-9+/=]+',
                            lambda match: f"assets/fonts/numbers_{next(font_index)}.ttf",
                            style.group(1).strip())
    (root / "style_snapshot.css").write_text(readable_style + "\n", encoding="utf-8", newline="\n")
    print("Updated index.html and style_snapshot.css")


if __name__ == "__main__":
    main()
