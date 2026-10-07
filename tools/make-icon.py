# Draws src\dynarunfix.ico (the installer's icon): a dyno gauge with a wrench.
# Requires Pillow. Usage: python tools\make-icon.py
import math, os, struct, io
from PIL import Image, ImageDraw

SS = 4  # supersampling

def draw(size):
    n = size * SS
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = n / 256.0
    small = size <= 24
    # rounded tile, dark blue
    pad = (8 if not small else 4) * s
    d.rounded_rectangle([pad, pad, n - pad, n - pad], radius=(52 if not small else 40) * s, fill=(22, 60, 120, 255))
    cx, cy, r = 128 * s, 140 * s, 88 * s
    w = (18 if not small else 30) * s
    # gauge arc: 225 deg sweep, last part red
    box = [cx - r, cy - r, cx + r, cy + r]
    d.arc(box, 135, 345, fill=(235, 240, 248, 255), width=int(w))
    d.arc(box, 345, 405, fill=(240, 80, 60, 255), width=int(w))
    if not small:  # tick marks
        for i in range(9):
            a = math.radians(135 + i * 270 / 8)
            r1, r2 = r - w - 6 * s, r - w - 22 * s
            d.line([cx + r1 * math.cos(a), cy + r1 * math.sin(a), cx + r2 * math.cos(a), cy + r2 * math.sin(a)],
                   fill=(235, 240, 248, 200), width=int(6 * s))
    # needle pointing up-right
    a = math.radians(300)
    L = r - (30 if not small else 20) * s
    nw = (14 if not small else 24) * s
    d.line([cx, cy, cx + L * math.cos(a), cy + L * math.sin(a)], fill=(255, 196, 0, 255), width=int(nw))
    hub = (20 if not small else 26) * s
    d.ellipse([cx - hub, cy - hub, cx + hub, cy + hub], fill=(255, 196, 0, 255))
    if not small:
        # wrench badge, bottom right
        bx, by, br = 190 * s, 196 * s, 46 * s
        d.ellipse([bx - br, by - br, bx + br, by + br], fill=(46, 160, 90, 255), outline=(22, 60, 120, 255), width=int(8 * s))
        ang = math.radians(-45)
        ux, uy = math.cos(ang), math.sin(ang)
        hl = 26 * s
        d.line([bx - ux * hl, by - uy * hl, bx + ux * hl * 0.6, by + uy * hl * 0.6], fill=(255, 255, 255, 255), width=int(12 * s))
        jx, jy, jr = bx + ux * hl * 0.75, by + uy * hl * 0.75, 15 * s
        d.ellipse([jx - jr, jy - jr, jx + jr, jy + jr], fill=(255, 255, 255, 255))
        # jaw opening
        ox, oy = jx + ux * 9 * s, jy + uy * 9 * s
        d.line([ox - ux * 2 * s, oy - uy * 2 * s, ox + ux * 14 * s, oy + uy * 14 * s], fill=(46, 160, 90, 255), width=int(10 * s))
    return img.resize((size, size), Image.LANCZOS)

def bmp_entry(img):
    # 32-bit BMP (BITMAPINFOHEADER, bottom-up, AND mask) - readable by Windows XP
    w, h = img.size
    px = img.tobytes("raw", "BGRA")
    rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)][::-1]
    mask_stride = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(mask_stride)
        for x in range(w):
            if img.getpixel((x, y))[3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(px) + len(mask), 0, 0, 0, 0)
    return hdr + b"".join(rows) + bytes(mask)

def png_entry(img):
    b = io.BytesIO(); img.save(b, "PNG"); return b.getvalue()

sizes = [16, 20, 24, 32, 40, 48, 64, 256]
entries = [(sz, png_entry(draw(sz)) if sz >= 256 else bmp_entry(draw(sz))) for sz in sizes]
out = struct.pack("<HHH", 0, 1, len(entries))
off = 6 + 16 * len(entries)
for sz, data in entries:
    out += struct.pack("<BBBBHHII", sz % 256, sz % 256, 0, 0, 1, 32, len(data), off)
    off += len(data)
out += b"".join(d for _, d in entries)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
open(os.path.join(root, "src", "dynarunfix.ico"), "wb").write(out)
draw(256).save(os.path.join(root, "build", "icon-preview.png")) if os.path.isdir(os.path.join(root, "build")) else None
