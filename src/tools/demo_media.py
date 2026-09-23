#!/usr/bin/env python3
"""Renders the README's demo videos and GIFs.

The app's own surfaces -- taskbar widget, hover card, settings window and
confetti -- come from demo_frames, which drives the real views offscreen with a
scripted pointer. This script paints everything Windows would supply around
them (wallpaper, a Windows 11 taskbar, the native context menu, the
pointer), hands that to demo_frames, and encodes the frames it streams
back with ffmpeg.

    python src/tools/demo_media.py                 # every scene
    python src/tools/demo_media.py hover menu      # just these
    python src/tools/demo_media.py --art-only      # paint the desktop, render nothing

Needs Pillow, numpy and ffmpeg on PATH, and a built demo_frames (cmake --build
build --config Release --target demo_frames). Reruns are deterministic.
"""
import argparse
import json
import math
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "media"
WORK = ROOT / "build" / "media"

# A 1440x900 desktop at 200%: the app and everything around it render at 2x, so
# the camera can zoom in on the taskbar without softening text.
SCALE = 2
SCREEN_W, SCREEN_H = 1440, 900
TASKBAR_H = 48
# The demo's "now" (UTC). demo_frames reads it from desktop.json, so the tray
# clock and the reset times in the app agree.
NOW = 1790488831
SCENES = ["hover", "settings", "providers", "modes", "menu", "reset"]
# GIFs at 60% of the video's width: settings text stays legible in the wide shots.
GIF_WIDTH, GIF_FPS = 960, 30

FONTS = Path("C:/Windows/Fonts")
CURSORS = Path("C:/Windows/Cursors")


def font(name, size):
    """`size` is in DIPs."""
    for candidate in ([name] if isinstance(name, str) else name):
        path = FONTS / candidate
        if path.exists():
            return ImageFont.truetype(str(path), round(size * SCALE))
    return ImageFont.load_default(round(size * SCALE))


UI = ["SegUIVar.ttf", "segoeui.ttf"]
UI_SEMIBOLD = ["seguisb.ttf", "segoeuib.ttf"]
ICONS = ["SegoeIcons.ttf", "segmdl2.ttf"]


def px(value):
    return round(value * SCALE)


# ---------------------------------------------------------------- drawing helpers

def supersampled(size, draw, factor=4):
    """Draws at `factor` times `size` with draw(ImageDraw, k) and downsamples, for
    antialiased shapes; `k` converts DIPs to supersampled pixels."""
    w, h = size
    big = Image.new("RGBA", (w * factor, h * factor), (0, 0, 0, 0))
    draw(ImageDraw.Draw(big), SCALE * factor)
    return big.resize((w, h), Image.LANCZOS)


def glyph(char, size, color, fonts=ICONS):
    """A single icon-font glyph as a tight RGBA image."""
    f = font(fonts, size)
    left, top, right, bottom = f.getbbox(char)
    image = Image.new("RGBA", (right - left + 2, bottom - top + 2), (0, 0, 0, 0))
    ImageDraw.Draw(image).text((1 - left, 1 - top), char, font=f, fill=color)
    return image


def paste_center(canvas, image, cx, cy):
    """Alpha-composites `image` centred on (cx, cy) in DIPs."""
    canvas.alpha_composite(image, (round(cx * SCALE - image.width / 2), round(cy * SCALE - image.height / 2)))


def shadow(size, rect, radius, blur, alpha, offset=(0, 0)):
    """A soft drop shadow for a rounded rect (DIPs) on a canvas of `size` pixels."""
    layer = Image.new("L", size, 0)
    x, y, w, h = rect
    ImageDraw.Draw(layer).rounded_rectangle(
        [px(x + offset[0]), px(y + offset[1]), px(x + w + offset[0]), px(y + h + offset[1])],
        radius=px(radius), fill=alpha)
    layer = layer.filter(ImageFilter.GaussianBlur(px(blur)))
    black = Image.new("RGBA", size, (0, 0, 0, 255))
    black.putalpha(layer)
    return black


# ---------------------------------------------------------------- wallpaper

def wallpaper():
    """A dark, softly lit abstract wallpaper, in the spirit of Windows 11's
    default without copying it."""
    w, h = SCREEN_W * SCALE, SCREEN_H * SCALE
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = x / w, y / h
    base = np.array([10, 14, 30], np.float32)
    top = np.array([22, 28, 58], np.float32)
    image = base + (top - base) * (1 - v)[..., None] * 0.9
    # Two overlapping translucent ribbons sweeping across the screen.
    def ribbon(center, amplitude, freq, phase, width, color, strength):
        curve = center + amplitude * np.sin(u * freq * math.pi + phase)
        d = (v - curve) / width
        glow = np.exp(-d * d) * strength
        return glow[..., None] * np.array(color, np.float32)
    image += ribbon(0.42, 0.18, 1.1, 0.4, 0.16, [40, 90, 200], 0.85)
    image += ribbon(0.55, 0.22, 0.9, 2.2, 0.10, [110, 70, 210], 0.65)
    image += ribbon(0.36, 0.12, 1.6, 1.3, 0.05, [90, 170, 255], 0.55)
    # A bloom of light towards the upper right.
    d2 = ((u - 0.72) / 0.35) ** 2 + ((v - 0.28) / 0.30) ** 2
    image += np.exp(-d2)[..., None] * np.array([55, 75, 150], np.float32) * 0.8
    # Vignette.
    vig = ((u - 0.5) ** 2 + (v - 0.45) ** 2) * 1.2
    image *= (1 - vig * 0.55)[..., None]
    image = np.clip(image, 0, 255).astype(np.uint8)
    return Image.fromarray(image, "RGB").convert("RGBA").filter(ImageFilter.GaussianBlur(3))


# ---------------------------------------------------------------- taskbar

def app_icon(kind, size=24):
    """Generic app icons for the taskbar: no product logos beyond the Start button."""
    n = px(size)
    def draw(d, k):
        s = size * k
        if kind == "start":
            g = s * 0.06
            q = (s - g) / 2 * 0.86
            o = (s - (2 * q + g)) / 2
            for i in range(2):
                for j in range(2):
                    x0, y0 = o + i * (q + g), o + j * (q + g)
                    d.rounded_rectangle([x0, y0, x0 + q, y0 + q], radius=s * 0.03,
                                        fill=(77 + 30 * j, 180 + 20 * j, 255, 255))
        elif kind == "folder":
            d.rounded_rectangle([s * .08, s * .18, s * .5, s * .4], radius=s * .06, fill=(225, 160, 50, 255))
            d.rounded_rectangle([s * .08, s * .26, s * .92, s * .84], radius=s * .07, fill=(232, 168, 56, 255))
            d.rounded_rectangle([s * .08, s * .36, s * .92, s * .84], radius=s * .07, fill=(255, 202, 79, 255))
        elif kind == "browser":
            d.ellipse([s * .08, s * .08, s * .92, s * .92], fill=(20, 130, 210, 255))
            d.ellipse([s * .08, s * .08, s * .92, s * .92], outline=(90, 200, 250, 255), width=int(s * .06))
            d.ellipse([s * .3, s * .08, s * .7, s * .92], outline=(170, 230, 255, 255), width=int(s * .05))
            d.line([s * .1, s * .5, s * .9, s * .5], fill=(170, 230, 255, 255), width=int(s * .05))
            d.line([s * .18, s * .3, s * .82, s * .3], fill=(170, 230, 255, 255), width=int(s * .04))
            d.line([s * .18, s * .7, s * .82, s * .7], fill=(170, 230, 255, 255), width=int(s * .04))
        elif kind == "terminal":
            d.rounded_rectangle([s * .06, s * .12, s * .94, s * .88], radius=s * .1, fill=(40, 40, 40, 255),
                                outline=(110, 110, 110, 255), width=max(1, int(s * .04)))
            d.line([s * .24, s * .36, s * .42, s * .5, s * .24, s * .64], fill=(240, 240, 240, 255),
                   width=int(s * .07), joint="curve")
            d.line([s * .48, s * .66, s * .74, s * .66], fill=(240, 240, 240, 255), width=int(s * .07))
        elif kind == "editor":
            d.rounded_rectangle([s * .06, s * .06, s * .94, s * .94], radius=s * .18, fill=(88, 76, 214, 255))
            w = int(s * .075)
            d.line([s * .38, s * .32, s * .22, s * .5, s * .38, s * .68], fill=(255, 255, 255, 255), width=w,
                   joint="curve")
            d.line([s * .62, s * .32, s * .78, s * .5, s * .62, s * .68], fill=(255, 255, 255, 255), width=w,
                   joint="curve")
        elif kind == "mail":
            d.rounded_rectangle([s * .06, s * .18, s * .94, s * .82], radius=s * .1, fill=(0, 120, 212, 255))
            d.line([s * .12, s * .26, s * .5, s * .56, s * .88, s * .26], fill=(200, 235, 255, 255),
                   width=int(s * .07), joint="curve")
    return supersampled((n, n), lambda d, k: draw(d, k * size / size))


def taskbar_layout():
    """Button and tray geometry in DIPs, shared with demo_frames as occupied space."""
    y = SCREEN_H - TASKBAR_H
    buttons = ["start", "search", "taskview", "folder", "browser", "terminal", "editor", "mail"]
    pitch, bw = 44, 40
    total = len(buttons) * pitch - (pitch - bw)
    left = (SCREEN_W - total) / 2
    rects = [(left + i * pitch, y + 4, bw, 40) for i in range(len(buttons))]
    # The tray, right to left: show-desktop sliver, bell, clock, network and volume, overflow chevron.
    right = SCREEN_W - 10
    bell = (right - 36, y + 4, 36, 40)
    clock = (bell[0] - 84, y + 4, 84, 40)
    status = (clock[0] - 66, y + 4, 66, 40)
    chevron = (status[0] - 30, y + 4, 30, 40)
    return {"buttons": list(zip(buttons, rects)), "bell": bell, "clock": clock, "status": status,
            "chevron": chevron, "center": (left, y, total, TASKBAR_H),
            "tray": (chevron[0], y, SCREEN_W - chevron[0], TASKBAR_H)}


def taskbar(canvas, layout):
    size = canvas.size
    y = SCREEN_H - TASKBAR_H
    # Mica-like: the wallpaper, heavily blurred and darkened.
    strip = canvas.crop((0, px(y) - px(60), size[0], size[1])).filter(ImageFilter.GaussianBlur(px(40)))
    strip = strip.crop((0, px(60), size[0], strip.height))
    dark = Image.new("RGBA", strip.size, (28, 28, 30, 232))
    strip.alpha_composite(dark)
    canvas.paste(strip, (0, px(y)))
    draw = ImageDraw.Draw(canvas)
    draw.rectangle([0, px(y), size[0], px(y) + max(1, SCALE // 2) - 1], fill=(62, 62, 66, 255))
    white = (255, 255, 255, 255)
    for kind, (bx, by, bw, bh) in layout["buttons"]:
        cx, cy = bx + bw / 2, by + bh / 2
        if kind == "search":
            paste_center(canvas, glyph("\uE721", 16, white), cx, cy)
        elif kind == "taskview":
            canvas.alpha_composite(supersampled(size, lambda d, k: (
                d.rounded_rectangle([(cx - 10) * k, (cy - 7) * k, (cx + 3) * k, (cy + 7) * k], radius=2 * k,
                                    outline=white, width=int(1.3 * k)),
                d.rounded_rectangle([(cx + 1) * k, (cy - 10) * k, (cx + 11) * k, (cy + 4) * k], radius=2 * k,
                                    fill=(28, 28, 30, 255), outline=white, width=int(1.3 * k)))))
        else:
            paste_center(canvas, app_icon(kind), cx, cy)
        # Running but minimised: a clean desktop with nothing focused.
        if kind in ("folder", "browser", "terminal"):
            canvas.alpha_composite(supersampled(size, lambda d, k: d.rounded_rectangle(
                [(cx - 3) * k, (by + bh - 3) * k, (cx + 3) * k, (by + bh) * k], radius=1.5 * k,
                fill=(160, 160, 160, 255))))
    # Tray.
    x, cy = layout["chevron"][0] + layout["chevron"][2] / 2, y + TASKBAR_H / 2
    paste_center(canvas, glyph("\uE70E", 10, white), x, cy)
    sx = layout["status"][0] + 18
    for char in ["\uE701", "\uE767"]:
        paste_center(canvas, glyph(char, 15, white), sx, cy)
        sx += 30
    cx = layout["clock"][0] + layout["clock"][2] - 8
    t = time.localtime(NOW)
    ui = font(UI, 12)
    draw.text((px(cx), px(cy - 9)), time.strftime("%H:%M", t), font=ui, fill=white, anchor="rm")
    draw.text((px(cx), px(cy + 9)), time.strftime("%d/%m/%Y", t), font=ui, fill=white, anchor="rm")
    bell = layout["bell"]
    paste_center(canvas, glyph("\uEA8F", 15, white), bell[0] + bell[2] / 2, cy)


# ---------------------------------------------------------------- native context menu

# Windows 11 draws a Win32 app's TrackPopupMenu light unless the app opts into dark
# menus, which BarTab does not.
MENU_ITEMS = ["Settings", "Start at boot", "Quit"]
MENU_W, MENU_ITEM_H, MENU_PAD = 190, 30, 4
MENU_MARGIN = 24  # shadow room around the panel in the sprite


def menu_sprite(checked, highlight):
    h = MENU_PAD * 2 + MENU_ITEM_H * len(MENU_ITEMS)
    size = (px(MENU_W + 2 * MENU_MARGIN), px(h + 2 * MENU_MARGIN))
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    panel = (MENU_MARGIN, MENU_MARGIN, MENU_W, h)
    image.alpha_composite(shadow(size, panel, 8, 10, 70, (0, 6)))
    image.alpha_composite(shadow(size, panel, 8, 1, 40))
    x0, y0 = MENU_MARGIN, MENU_MARGIN
    image.alpha_composite(supersampled(size, lambda d, k: d.rounded_rectangle(
        [x0 * k, y0 * k, (x0 + MENU_W) * k, (y0 + h) * k], radius=8 * k, fill=(249, 249, 249, 255),
        outline=(214, 214, 214, 255), width=max(1, k // 2))))
    draw = ImageDraw.Draw(image)
    ui = font(UI, 12)
    items = []
    for i, label in enumerate(MENU_ITEMS):
        ix, iy = x0 + 4, y0 + MENU_PAD + i * MENU_ITEM_H
        items.append([px(ix - x0), px(iy - y0), px(MENU_W - 8), px(MENU_ITEM_H)])
        if i == highlight:
            image.alpha_composite(supersampled(size, lambda d, k: d.rounded_rectangle(
                [ix * k, (iy + 1) * k, (ix + MENU_W - 8) * k, (iy + MENU_ITEM_H - 1) * k], radius=4 * k,
                fill=(0, 0, 0, 15))))
        if label == "Start at boot" and checked:
            paste_center(image, glyph("\uE73E", 11, (26, 26, 26, 255)), ix + 16, iy + MENU_ITEM_H / 2)
        draw.text((px(ix + 34), px(iy + MENU_ITEM_H / 2)), label, font=ui, fill=(26, 26, 26, 255), anchor="lm")
    return image, {"panel": [px(MENU_MARGIN), px(MENU_MARGIN), px(MENU_W), px(h)], "items": items}


# ---------------------------------------------------------------- cursors

def read_cursor(path, size):
    """The `size` px image and hotspot from a .cur file, alpha intact."""
    data = path.read_bytes()
    _, kind, count = struct.unpack("<HHH", data[:6])
    for i in range(count):
        w, h, _, _, hx, hy, length, offset = struct.unpack("<BBBBHHII", data[6 + 16 * i:22 + 16 * i])
        if (w or 256) != size:
            continue
        blob = data[offset:offset + length]
        if blob[:4] == b"\x89PNG":
            import io
            return Image.open(io.BytesIO(blob)).convert("RGBA"), (hx, hy)
        header = struct.unpack("<IiiHH", blob[:16])
        if header[4] != 32:
            continue
        pixels = np.frombuffer(blob[header[0]:header[0] + size * size * 4], np.uint8).reshape(size, size, 4)
        rgba = pixels[::-1, :, [2, 1, 0, 3]].copy()
        return Image.fromarray(rgba, "RGBA"), (hx, hy)
    return None


def drawn_arrow():
    """The standard arrow, for machines without Windows' cursor files."""
    points = [(0, 0), (0, 17), (4, 13), (7, 20), (10, 19), (7, 12), (12, 12)]
    image = supersampled((px(16), px(22)), lambda d, k: d.polygon(
        [(1 * k / 2 + x * k * 0.95, 1 * k / 2 + y * k * 0.95) for x, y in points], fill=(0, 0, 0, 255),
        outline=(255, 255, 255, 255), width=int(k * 0.9)))
    return image, (1, 1)


def cursor(name):
    path = CURSORS / f"{name}.cur"
    found = read_cursor(path, px(32)) if path.exists() else None
    return found or drawn_arrow()


# ---------------------------------------------------------------- output

def write_raw(image, path):
    """Premultiplied BGRA after an 8-byte width/height header: demo_frames' format."""
    rgba = np.asarray(image.convert("RGBA"), np.float32)
    alpha = rgba[..., 3:4] / 255.0
    rgb = rgba[..., :3] * alpha
    bgra = np.concatenate([rgb[..., ::-1], rgba[..., 3:4]], axis=-1)
    bgra = np.clip(np.round(bgra), 0, 255).astype(np.uint8)
    with open(path, "wb") as out:
        out.write(struct.pack("<II", image.width, image.height))
        out.write(bgra.tobytes())


def rect_px(rect):
    return [px(v) for v in rect]


def paint_desktop(work):
    work.mkdir(parents=True, exist_ok=True)
    layout = taskbar_layout()
    desktop = wallpaper()
    taskbar(desktop, layout)
    desktop.convert("RGB").save(work / "desktop.png")
    write_raw(desktop, work / "desktop.bgra")

    menus = {}
    geometry = None
    for checked in (True, False):
        for highlight in (-1, 0, 1, 2):
            image, geometry = menu_sprite(checked, highlight)
            name = f"menu-{'on' if checked else 'off'}-{highlight + 1}.bgra"
            write_raw(image, work / name)
            if checked and highlight == 1:
                image.save(work / "menu-preview.png")
            menus[f"{'on' if checked else 'off'}{highlight + 1}"] = name

    cursors = {}
    for key, name in (("arrow", "aero_arrow"), ("hand", "aero_link")):
        image, hotspot = cursor(name)
        write_raw(image, work / f"cursor-{key}.bgra")
        cursors[key] = {"file": f"cursor-{key}.bgra", "hotspot": list(hotspot)}

    spec = {
        "scale": SCALE, "now": NOW,
        "screen": [px(SCREEN_W), px(SCREEN_H)],
        "taskbar": rect_px((0, SCREEN_H - TASKBAR_H, SCREEN_W, TASKBAR_H)),
        "occupied": [rect_px(layout["center"]), rect_px(layout["tray"])],
        "desktop": "desktop.bgra",
        "menu": {"sprites": menus, **geometry},
        "cursors": cursors,
    }
    (work / "desktop.json").write_text(json.dumps(spec, indent=2))
    return spec


def find_frames_tool():
    for config in ("Release", "RelWithDebInfo", "Debug", ""):
        for name in ("demo_frames.exe", "demo_frames"):
            candidate = ROOT / "build" / config / name
            if candidate.exists():
                return candidate
    sys.exit("demo_frames not found: cmake --build build --config Release --target demo_frames")


def render(tool, scene, work, out):
    """Streams one scene from demo_frames into a lossless intermediate, then encodes
    the MP4 and GIF from it."""
    process = subprocess.Popen([str(tool), str(work), scene], stdout=subprocess.PIPE)
    header = json.loads(process.stdout.readline())
    width, height, fps = header["width"], header["height"], header["fps"]
    master = work / f"{scene}.mkv"
    ffmpeg = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
    encoder = subprocess.Popen(ffmpeg + [
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{width}x{height}", "-r", str(fps), "-i", "-",
        "-c:v", "ffv1", "-level", "3", str(master)], stdin=subprocess.PIPE)
    frames = 0
    frame_bytes = width * height * 3
    while True:
        chunk = process.stdout.read(frame_bytes)
        if not chunk:
            break
        encoder.stdin.write(chunk)
        frames += 1
    encoder.stdin.close()
    if process.wait() or encoder.wait():
        sys.exit(f"{scene}: rendering failed")
    out.mkdir(parents=True, exist_ok=True)
    mp4, gif = out / f"{scene}.mp4", out / f"{scene}.gif"
    subprocess.run(ffmpeg + ["-i", str(master), "-c:v", "libx264", "-preset", "slow", "-crf", "20",
                             "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(mp4)], check=True)
    palette = (f"fps={GIF_FPS},scale={GIF_WIDTH}:-1:flags=lanczos,split[a][b];"
               "[a]palettegen=max_colors=192:stats_mode=diff[p];"
               "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle")
    subprocess.run(ffmpeg + ["-i", str(master), "-filter_complex", palette, "-loop", "0", str(gif)], check=True)
    print(f"{scene}: {frames} frames, {mp4.stat().st_size / 1e6:.1f} MB mp4, {gif.stat().st_size / 1e6:.1f} MB gif")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenes", nargs="*", default=SCENES, help=f"any of: {', '.join(SCENES)}")
    parser.add_argument("--art-only", action="store_true", help="paint the desktop art and stop")
    args = parser.parse_args()
    unknown = set(args.scenes) - set(SCENES)
    if unknown:
        sys.exit(f"unknown scene(s): {', '.join(sorted(unknown))}")
    paint_desktop(WORK)
    if args.art_only:
        print(f"art written to {WORK}")
        return
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg must be on PATH")
    tool = find_frames_tool()
    for scene in args.scenes:
        render(tool, scene, WORK, OUT)


if __name__ == "__main__":
    main()
