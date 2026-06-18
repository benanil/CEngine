from pathlib import Path


W = 64
H = 64
OUT_PATH = Path("Source/UI/WindowButtonIcons.inl")


def blank():
    return [[0 for _ in range(W)] for _ in range(H)]


def rounded_rect(pixels, x0, y0, x1, y1, r):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            cx = x0 + r if x < x0 + r else x1 - r - 1 if x >= x1 - r else x
            cy = y0 + r if y < y0 + r else y1 - r - 1 if y >= y1 - r else y
            if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r:
                pixels[y][x] = 1


def rect_outline(pixels, x0, y0, x1, y1, thickness, r=3):
    rounded_rect(pixels, x0, y0, x1, y0 + thickness, r)
    rounded_rect(pixels, x0, y1 - thickness, x1, y1, r)
    rounded_rect(pixels, x0, y0, x0 + thickness, y1, r)
    rounded_rect(pixels, x1 - thickness, y0, x1, y1, r)


def icon_rows(pixels):
    rows = []
    for row in pixels:
        mask = 0
        for x, v in enumerate(row):
            if v:
                mask |= 1 << x
        rows.append(mask)
    return rows


def make_icons():
    icons = {}

    pixels = blank()
    rounded_rect(pixels, 14, 29, 50, 35, 3)
    icons["Minimize"] = pixels

    pixels = blank()
    rect_outline(pixels, 15, 15, 49, 49, 5)
    icons["Maximize"] = pixels

    pixels = blank()
    rect_outline(pixels, 12, 23, 42, 51, 5)
    rect_outline(pixels, 23, 12, 52, 41, 5)
    icons["Restore"] = pixels

    pixels = blank()
    for i in range(17, 47):
        rounded_rect(pixels, i - 3, i - 3, i + 4, i + 4, 3)
        j = 63 - i
        rounded_rect(pixels, i - 3, j - 3, i + 4, j + 4, 3)
    icons["Close"] = pixels

    return icons


def write_icon(f, name, pixels):
    rows = icon_rows(pixels)
    f.write(f"static const u64 Editor{name}IconRows[64] = {{\n")
    for i in range(0, len(rows), 8):
        f.write("    ")
        f.write(",".join(f"0x{row:016X}ull" for row in rows[i:i + 8]))
        f.write(",\n")
    f.write("};\n\n")


def main():
    icons = make_icons()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with OUT_PATH.open("w", encoding="ascii", newline="\n") as f:
        for name in ("Minimize", "Maximize", "Restore", "Close"):
            write_icon(f, name, icons[name])
    print(f"Wrote {OUT_PATH}")


if __name__ == "__main__":
    main()
