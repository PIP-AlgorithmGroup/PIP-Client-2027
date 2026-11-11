"""Render the welcome-page proposal; this tool is not an application dependency.

Uses Pillow and explicitly supplied fonts. Output is a static review image;
production QML must lay out real controls using its own font metrics.
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps


def render(background_path, output_path, font_dir):
    """Write a 1600x900 proposal using visible glyph bounds for placement.

    Owns image resources synchronously. Missing fonts/assets and output errors
    propagate to the caller. The supplied font files are read, never distributed.
    """
    image = ImageOps.fit(Image.open(background_path).convert('RGB'), (1600, 900),
                         method=Image.Resampling.LANCZOS).convert('RGBA')

    # Contrast stays behind the left menu; the trophy and sky keep their light.
    veil = Image.new('RGBA', image.size)
    pixels = veil.load()
    for y in range(900):
        for x in range(1600):
            left = 0.60 * max(0, 1 - x / 800)
            foot = 0.36 * max(0, (y - 820) / 80)
            pixels[x, y] = (10, 30, 48, round(255 * min(0.65, left + foot)))
    image = Image.alpha_composite(image, veil)

    def font(size, bold=False, latin=False):
        name = 'bahnschrift.ttf' if latin else ('msyhbd.ttc' if bold else 'msyh.ttc')
        return ImageFont.truetype(str(font_dir / name), size)

    def text(value, x, y, size, color='white', bold=False, latin=False,
             center_x=False, center_y=False, shadow=False):
        face = font(size, bold, latin)
        left, top, right, bottom = face.getbbox(value)
        # Font origins differ across CJK and Latin. Align the visible ink, not
        # nominal font size; center both strings on the same visual center.
        origin_x = x - (left + right) / 2 if center_x else x - left
        origin_y = y - (top + bottom) / 2 if center_y else y - top
        if shadow:
            layer = Image.new('RGBA', image.size)
            ImageDraw.Draw(layer).text((origin_x, origin_y + 2), value, font=face,
                                       fill=(5, 19, 34, 175))
            image.alpha_composite(layer.filter(ImageFilter.GaussianBlur(3)))
        ImageDraw.Draw(image).text((origin_x, origin_y), value, font=face, fill=color)

    def panel(box, fill, outline=None, radius=8):
        layer = Image.new('RGBA', image.size)
        ImageDraw.Draw(layer).rounded_rectangle(box, radius=radius, fill=fill,
                                                outline=outline, width=1)
        image.alpha_composite(layer)

    def line(points, fill, width=1):
        ImageDraw.Draw(image).line(points, fill=fill, width=width)

    # Small corner identity; title and menu share the x=84 visible edge.
    line([(84, 68), (84, 46), (101, 46), (108, 53), (101, 60), (84, 60)], 'white', 3)
    text('PIP / CLIENT', 124, 57, 22, latin=True, center_y=True, shadow=True)
    panel((1344, 37, 1516, 77), (17, 49, 74, 165), (223, 245, 255, 85), 20)
    text('SEASON 2027', 1430, 57, 16, latin=True, center_x=True, center_y=True)
    text('ROBOMASTER', 84, 200, 18, '#c4f4ff', latin=True, shadow=True)
    text('PIP', 84, 245, 106, latin=True, shadow=True)
    text('CLIENT', 84, 346, 82, latin=True, shadow=True)
    line([(84, 452), (138, 452)], '#b4f6ff', 3)
    text('机甲大师自定义终端', 84, 478, 25, shadow=True)
    text('选择赛事，准备出发。', 84, 522, 17, '#e3f1fa', shadow=True)

    # A compact tinted plate stabilizes contrast under both mode labels.
    # It avoids relying on shadows alone over bright, detailed scenery.
    panel((72, 576, 562, 822), (12, 38, 59, 181), (226, 246, 255, 95), 10)
    text('赛事模式', 92, 596, 14, '#e0effa')
    panel((88, 626, 546, 688), (8, 26, 43, 175), (220, 242, 255, 105), 5)
    panel((93, 631, 313, 683), (244, 250, 255, 255), radius=3)
    # Both mixed-size labels use identical visible centers, at y=657.
    text('RMUL', 110, 657, 24, '#173e59', latin=True, center_y=True)
    text('高校联盟赛', 195, 657, 15, '#173e59', center_y=True)
    text('RMUC', 330, 657, 24, latin=True, center_y=True)
    text('超级对抗赛', 420, 657, 15, center_y=True)

    # Equal left/right reserved space keeps the button label optically centered
    # even with an arrow on its right. A solid fill ensures stable contrast.
    draw = ImageDraw.Draw(image)
    draw.polygon([(88, 709), (546, 709), (546, 764), (532, 778), (88, 778)],
                 fill='#ffda80')
    line([(90, 711), (544, 711)], '#fff0bf')
    text('进入赛场', 317, 743, 25, '#183a4e', bold=True, center_x=True, center_y=True)
    line([(500, 743), (520, 743), (513, 736)], '#183a4e', 2)
    line([(520, 743), (513, 750)], '#183a4e', 2)
    text('当前：RMUL · 高校联盟赛', 92, 798, 14, '#e4f1fa', center_y=True)

    # Icons and their captions share one backing, rather than captions floating
    # directly over the bright scene. Each visible group uses the same bounds.
    for index, label in enumerate(('设置', '诊断', '帮助', '关于')):
        center_y = 490 + index * 77
        panel((1450, center_y - 26, 1540, center_y + 26),
              (12, 38, 59, 210), (221, 244, 255, 120), 26)
        x = 1471
        if index == 0:
            for offset, knob in ((-7, -3), (0, 4), (7, -1)):
                line([(x - 9, center_y + offset), (x + 9, center_y + offset)], 'white')
                line([(x + knob, center_y + offset - 2),
                      (x + knob, center_y + offset + 2)], 'white', 2)
        elif index == 1:
            line([(x - 10, center_y + 2), (x - 5, center_y + 2),
                  (x - 2, center_y - 7), (x + 3, center_y + 8),
                  (x + 6, center_y), (x + 10, center_y)], 'white', 2)
        else:
            text('?' if index == 2 else 'i', x, center_y, 22,
                 latin=True, center_x=True, center_y=True)
        text(label, 1488, center_y, 15, center_y=True)

    panel((76, 849, 328, 881), (12, 38, 59, 175), radius=16)
    text('v0.1.0', 92, 865, 14, latin=True, center_y=True)
    ImageDraw.Draw(image).ellipse((181, 862, 187, 868), fill='#c1d1de')
    text('尚未连接', 200, 865, 14, '#e3edf5', center_y=True)
    # The review-only watermark has separate backing and stays inside the frame.
    panel((1304, 845, 1536, 881), (12, 38, 59, 210), radius=18)
    text('视觉提案 · 非运行截图', 1420, 863, 14, '#e3edf5',
         center_x=True, center_y=True)
    image.convert('RGB').save(output_path, optimize=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font-dir', type=Path, required=True)
    parser.add_argument('--background', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    render(args.background, args.output, args.font_dir)
