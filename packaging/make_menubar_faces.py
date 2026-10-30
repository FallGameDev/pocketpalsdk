# Draws the outline faces for the Mac menu bar (36x36 px = 18pt @2x).
# Black-on-clear faces are "template" images, so macOS paints them
# white or black to match the menu bar. The "hurt" face is red on purpose.
from PIL import Image, ImageDraw
import os, sys

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 8                 # draw 8x bigger, then shrink for smooth edges
W = 36 * S
LINE = int(2.6 * S)   # outline thickness

def face(name, color=(0, 0, 0, 255), fill=None):
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = 3 * S
    # head
    d.ellipse([pad, pad, W - pad, W - pad], fill=fill, outline=color, width=LINE)
    cx, ey, ex = W / 2, 14.5 * S, 6 * S      # eye height and eye spacing
    dot = lambda x, y, r: d.ellipse([x - r, y - r, x + r, y + r], fill=color)
    arc = lambda box, a, b: d.arc(box, a, b, fill=color, width=LINE)
    line = lambda pts: d.line(pts, fill=color, width=LINE, joint="curve")

    if name == "happy":
        dot(cx - ex, ey, 2 * S); dot(cx + ex, ey, 2 * S)
        arc([cx - 7 * S, 13 * S, cx + 7 * S, 25 * S], 20, 160)          # smile
    elif name == "meh":
        dot(cx - ex, ey, 2 * S); dot(cx + ex, ey, 2 * S)
        line([(cx - 6 * S, 22.5 * S), (cx + 6 * S, 22.5 * S)])            # flat mouth
    elif name == "sad":
        dot(cx - ex, ey + S, 2 * S); dot(cx + ex, ey + S, 2 * S)
        arc([cx - 6.5 * S, 21 * S, cx + 6.5 * S, 30 * S], 200, 340)       # frown
    elif name == "hungry":
        # droopy worried eyes + wobbly sick mouth
        line([(cx - 8.5 * S, 15.5 * S), (cx - 3.5 * S, 13 * S)])
        line([(cx + 8.5 * S, 15.5 * S), (cx + 3.5 * S, 13 * S)])
        pts, x0 = [], cx - 7.5 * S
        for i in range(31):
            x = x0 + i * 0.5 * S
            y = 23 * S + (1.6 * S if (i // 5) % 2 else -1.6 * S) * (1 - abs((i % 5) - 2.5) / 2.5)
            pts.append((x, y))
        line(pts)
    elif name == "hurt":
        # sad face with > < eyes, drawn in red
        line([(cx - 8 * S, 12 * S), (cx - 4 * S, 14.5 * S), (cx - 8 * S, 17 * S)])
        line([(cx + 8 * S, 12 * S), (cx + 4 * S, 14.5 * S), (cx + 8 * S, 17 * S)])
        arc([cx - 6 * S, 21 * S, cx + 6 * S, 30 * S], 200, 340)
    elif name == "sleep":
        arc([cx - ex - 3 * S, ey - 4 * S, cx - ex + 3 * S, ey + 2 * S], 20, 160)
        arc([cx + ex - 3 * S, ey - 4 * S, cx + ex + 3 * S, ey + 2 * S], 20, 160)
        dot(cx, 23 * S, 1.8 * S)
    elif name == "none":
        # paw print for "no pet yet"
        dot(cx, 21 * S, 5 * S)
        for x, y in [(-6, 13.5), (-2, 10.5), (2, 10.5), (6, 13.5)]:
            dot(cx + x * S, y * S, 2 * S)
    small = img.resize((36, 36), Image.LANCZOS)
    small.save(os.path.join(OUT, f"face_{name}.png"))
    return small

faces = [face(n) for n in ["happy", "meh", "sad", "hungry", "sleep", "none"]]
faces.append(face("hurt", color=(255, 77, 94, 255), fill=(255, 77, 94, 70)))

# preview sheet on a dark and a light bar
sheet = Image.new("RGB", (36 * 7 + 16, 100), (30, 30, 30))
for i, f in enumerate(faces):
    white = f.copy()
    if i < 6:  # template faces show white on a dark bar
        px = white.load()
        for y in range(36):
            for x in range(36):
                r, g, b, a = px[x, y]; px[x, y] = (255, 255, 255, a)
    sheet.paste(white, (8 + i * 36, 8), white)
light = Image.new("RGB", (36 * 7 + 16, 50), (236, 236, 236))
for i, f in enumerate(faces):
    light.paste(f, (8 + i * 36, 7), f)
sheet.paste(light, (0, 50))
sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(sys.argv[2])
print("faces saved")
