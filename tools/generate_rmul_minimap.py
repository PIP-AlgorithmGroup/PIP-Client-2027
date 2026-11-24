"""从Silver原稿导出Qt可读取的静态地图，产品构建不运行此工具。

坐标沿用800×1200设计视口，不是导航坐标。素材维护需要Pillow，与应用图标工具相同。
"""

import base64
import copy
import hashlib
import io
import json
import xml.etree.ElementTree as element_tree
from pathlib import Path

from PIL import Image, ImageOps


SVG_NAMESPACE = "http://www.w3.org/2000/svg"
XLINK_NAMESPACE = "http://www.w3.org/1999/xlink"
REPOSITORY = Path(__file__).resolve().parents[1]
SOURCE_DIRECTORY = REPOSITORY / "docs/design/custom_terminal/assets/minimap"
OUTPUT_DIRECTORY = REPOSITORY / "resources/maps/rmul"


def convert_static_map(team: str, is_neutral: bool = False) -> bytes:
    """移除哨兵组，展开嵌套SVG，保留固定地形与符号并兼容Qt的xlink图像引用。

    is_neutral使用蓝方几何作为中性参考方向，仅将阵营维修符号去色，不暗示已知己方。
    视口或图像格式无法识别时直接报错，避免输出缺失地形的可加载空图。
    """
    root = element_tree.parse(SOURCE_DIRECTORY / f"silver_{team}.svg").getroot()
    if root.get("viewBox") != "0 0 800 1200":
        raise ValueError("Silver地图视口已变化，需重新核对坐标和缩放")
    root.attrib.clear()
    root.set("viewBox", "0 0 800 1200")
    for parent in list(root.iter()):
        for child in list(parent):
            if child.get("data-robot") is not None:
                parent.remove(child)
    for title in root.findall(f"{{{SVG_NAMESPACE}}}title"):
        root.remove(title)
    for parent in list(root.iter()):
        for node in list(parent):
            if node.tag != f"{{{SVG_NAMESPACE}}}image":
                continue
            href = node.attrib.pop("href")
            if href.startswith("data:image/svg+xml;base64,"):
                nested = element_tree.fromstring(base64.b64decode(href.split(",", 1)[1]))
                x, y, width, height = map(float, nested.attrib["viewBox"].split())
                if x != 0 or y != 0:
                    raise ValueError("内嵌地形视口原点发生变化")
                group = element_tree.Element(
                    f"{{{SVG_NAMESPACE}}}g",
                    {
                        "transform": (
                            f"translate({node.get('x')} {node.get('y')}) "
                            f"scale({float(node.get('width')) / width} "
                            f"{float(node.get('height')) / height})"
                        )
                    },
                )
                group.extend(copy.deepcopy(list(nested)))
                parent.insert(list(parent).index(node), group)
                parent.remove(node)
            elif href.startswith("data:image/png;base64,"):
                if is_neutral and node.get("data-field-symbol") in ("field_symbol_0", "field_symbol_4"):
                    with Image.open(io.BytesIO(base64.b64decode(href.split(",", 1)[1]))) as image:
                        rgba = image.convert("RGBA")
                        grey = ImageOps.grayscale(rgba).convert("RGBA")
                        grey.putalpha(rgba.getchannel("A"))
                        buffer = io.BytesIO()
                        grey.save(buffer, format="PNG")
                        href = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()
                node.set(f"{{{XLINK_NAMESPACE}}}href", href)
            else:
                raise ValueError("Silver地图包含未支持的图像来源")
    element_tree.indent(root, space="    ")
    return element_tree.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"


def main() -> None:
    """从仓库相对路径生成三张离线SVG和来源/输出摘要，不改写设计原稿。"""
    element_tree.register_namespace("", SVG_NAMESPACE)
    element_tree.register_namespace("xlink", XLINK_NAMESPACE)
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    manifest = {"source": {}, "output": {}}
    for team in ("blue", "red"):
        source = SOURCE_DIRECTORY / f"silver_{team}.svg"
        manifest["source"][source.relative_to(REPOSITORY).as_posix()] = hashlib.sha256(
            source.read_bytes()
        ).hexdigest()
    for view in ("blue", "red", "neutral"):
        contents = convert_static_map("blue" if view == "neutral" else view, view == "neutral")
        path = OUTPUT_DIRECTORY / f"silver_{view}.svg"
        path.write_bytes(contents)
        manifest["output"][path.name] = hashlib.sha256(contents).hexdigest()
    (OUTPUT_DIRECTORY / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=4) + "\n", encoding="utf-8", newline="\n"
    )


if __name__ == "__main__":
    main()
