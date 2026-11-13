"""Render bundled vector icons to PNG and multi-size ICO files.

Requires Pillow >= 9.1. Run from any directory; paths default to the repository beside this script.
The selected receiver uses the Qt SVG helper; historical rect/polygon artwork retains its
validated Pillow renderer. Normal application builds consume checked-in assets, not this tool.
"""

import argparse
import io
import struct
import subprocess
import xml.etree.ElementTree as element_tree
from pathlib import Path

from PIL import Image, ImageDraw


# Dimensions are physical pixels; Windows ICO stores at most 256 per embedded entry.
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
ICO_SIZES = ICON_SIZES[:-1]
SUPERSAMPLING = 4


def render_icon(source_path: Path, size: int) -> Image.Image:
    """Render rect/polygon geometry at 4x resolution; preserve SVG transparency and sRGB colors."""
    root = element_tree.parse(source_path).getroot()
    if root.tag != "{http://www.w3.org/2000/svg}svg":
        raise ValueError("The icon renderer expects an SVG root element")
    if set(root.attrib) - {"width", "height", "viewBox"}:
        raise ValueError("The icon renderer does not support root styles or transformations")
    if root.get("viewBox") != "0 0 512 512":
        raise ValueError("The icon renderer expects a 512 x 512 SVG viewBox")
    scale = size * SUPERSAMPLING / 512
    image = Image.new("RGBA", (size * SUPERSAMPLING, size * SUPERSAMPLING))
    drawing = ImageDraw.Draw(image)
    for shape in root:
        tag = shape.tag.rsplit("}", 1)[-1]
        if tag in ("title", "desc"):
            continue
        allowed = {
            "rect": {"x", "y", "width", "height", "rx", "fill"},
            "polygon": {"points", "fill"},
        }
        if tag not in allowed or set(shape.attrib) - allowed[tag]:
            raise ValueError(f"Unsupported SVG element or attribute: {tag} {shape.attrib}")
        fill = shape.get("fill")
        if not fill:
            raise ValueError(f"SVG {tag} must have an explicit solid fill")
        if tag == "rect":
            left = float(shape.get("x", 0)) * scale
            top = float(shape.get("y", 0)) * scale
            right = left + float(shape.attrib["width"]) * scale
            bottom = top + float(shape.attrib["height"]) * scale
            # Pillow includes the far edge; exclude one high-resolution pixel before filtering.
            bounds = (left, top, right - 1, bottom - 1)
            radius = float(shape.get("rx", 0)) * scale
            drawing.rounded_rectangle(bounds, radius=radius, fill=fill)
        else:
            points = [
                tuple(float(value) * scale for value in point.split(","))
                for point in shape.attrib["points"].split()
            ]
            drawing.polygon(points, fill=fill)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def write_ico(images: dict[int, Image.Image], output_path: Path) -> None:
    """Write PNG-compressed ICO entries for every Windows size, with deterministic ordering."""
    payloads = []
    for size in ICO_SIZES:
        stream = io.BytesIO()
        images[size].save(stream, format="PNG", optimize=False, compress_level=9)
        payloads.append((size, stream.getvalue()))
    offset = 6 + 16 * len(payloads)
    entries = []
    for size, payload in payloads:
        # ICO stores 256 as zero and keeps byte offsets relative to the file header.
        dimension = size if size < 256 else 0
        entries.append(
            struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(payload), offset)
        )
        offset += len(payload)
    with output_path.open("wb") as output:
        output.write(struct.pack("<HHH", 0, 1, len(payloads)))
        output.write(b"".join(entries))
        output.write(b"".join(payload for _, payload in payloads))


def render_qt_icons(renderer: Path, source: Path, small_source: Path,
                    output_directory: Path) -> dict[int, Image.Image]:
    """Run the native Qt helper and validate owned RGBA images before writing the ICO.

    All paths are passed as arguments without a shell. The dedicated small source supplies only
    16px; the standard source supplies every larger size. Missing inputs or failed rendering abort
    generation with the helper's diagnostic instead of silently falling back to different artwork.
    """
    result = subprocess.run(
        [str(renderer.resolve(strict=True)), str(source.resolve(strict=True)),
         str(output_directory.resolve()), str(small_source.resolve(strict=True))],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        diagnostic = result.stderr.strip() or result.stdout.strip() or "No diagnostic was emitted"
        raise ValueError(f"Qt icon renderer failed ({result.returncode}): {diagnostic}")
    images = {}
    for size in ICON_SIZES:
        image_path = output_directory / f"pip_client_{size}.png"
        with Image.open(image_path) as image:
            if image.size != (size, size) or image.mode != "RGBA":
                raise ValueError(f"Qt renderer produced an invalid {size}px RGBA image: {image_path}")
            # The helper's files are closed before the caller normalizes PNG compression.
            images[size] = image.copy()
    return images


def main() -> None:
    """Generate assets beside the SVG, or in an explicit directory for reproducibility checks."""
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("receiver", "viewlink", "circuit", "signal"),
                        default="receiver")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--small-source", type=Path,
                        help="16px SVG for the receiver; defaults to pip_client_16.svg beside source")
    parser.add_argument("--renderer", type=Path,
                        help="Native pip_icon_renderer executable, required for the receiver")
    parser.add_argument("--output-dir", type=Path)
    arguments = parser.parse_args()
    variant_directory = repository / "resources" / "app" / "variants" / arguments.variant
    source = arguments.source or variant_directory / "pip_client.svg"
    output_directory = arguments.output_dir or variant_directory
    output_directory.mkdir(parents=True, exist_ok=True)
    if arguments.variant == "receiver":
        if arguments.renderer is None:
            parser.error("receiver requires --renderer; build tools/icon_renderer with the selected Qt kit")
        small_source = arguments.small_source or source.with_name("pip_client_16.svg")
        try:
            images = render_qt_icons(arguments.renderer, source, small_source, output_directory)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    else:
        if arguments.small_source is not None or arguments.renderer is not None:
            parser.error("--renderer and --small-source apply only to the receiver variant")
        images = {size: render_icon(source, size) for size in ICON_SIZES}
    for size, image in images.items():
        image.save(output_directory / f"pip_client_{size}.png", optimize=False, compress_level=9)
    write_ico(images, output_directory / "pip_client.ico")
    print(f"Generated {len(images)} PNGs and one {len(ICO_SIZES)}-size ICO")


if __name__ == "__main__":
    main()
