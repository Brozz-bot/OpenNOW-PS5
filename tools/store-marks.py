#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Flatten the store brand SVGs in assets/stores into src/ui/store_marks.inc.

Each mark becomes polygon contours in a 24x24 box, centered and scaled to fit,
so the UI can rasterize it at any size without image assets.
Usage: python3 tools/store-marks.py
"""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARKS = ["steam", "epic", "ubisoft", "battlenet", "xbox", "gog", "ea", "nvidia", "gaijin"]
TOLERANCE = 0.01

TOKEN = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def tokens(d):
    return TOKEN.findall(d)


def flatten_cubic(p0, p1, p2, p3, out, depth=0):
    dx, dy = p3[0] - p0[0], p3[1] - p0[1]
    length = math.hypot(dx, dy)
    if length > 1e-9:
        deviation = max(abs((p[0] - p0[0]) * dy - (p[1] - p0[1]) * dx) / length for p in (p1, p2))
    else:
        deviation = max(math.hypot(p[0] - p0[0], p[1] - p0[1]) for p in (p1, p2))
    if depth >= 12 or deviation <= TOLERANCE:
        out.append(p3)
        return
    m = lambda a, b: ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    p01, p12, p23 = m(p0, p1), m(p1, p2), m(p2, p3)
    p012, p123 = m(p01, p12), m(p12, p23)
    mid = m(p012, p123)
    flatten_cubic(p0, p01, p012, mid, out, depth + 1)
    flatten_cubic(mid, p123, p23, p3, out, depth + 1)


def arc_to_cubics(p0, rx, ry, angle, large, sweep, p1):
    if p0 == p1:
        return []
    if rx == 0 or ry == 0:
        return [(p0, p0, p1, p1)]
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(angle)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1 = cp * dx + sp * dy
    y1 = -sp * dx + cp * dy
    lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    coef = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        coef = -coef
    cxp, cyp = coef * rx * y1 / ry, -coef * ry * x1 / rx
    cx = cp * cxp - sp * cyp + (p0[0] + p1[0]) / 2
    cy = sp * cxp + cp * cyp + (p0[1] + p1[1]) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry)
    dt = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    segments = max(1, int(math.ceil(abs(dt) / (math.pi / 2))))
    step = dt / segments
    k = 4 / 3 * math.tan(step / 4)
    result = []
    point = lambda t: (cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp, cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp)
    deriv = lambda t: (-rx * math.sin(t) * cp - ry * math.cos(t) * sp, -rx * math.sin(t) * sp + ry * math.cos(t) * cp)
    t = t1
    start = p0
    for i in range(segments):
        t2 = t + step
        end = p1 if i == segments - 1 else point(t2)
        d1, d2 = deriv(t), deriv(t2)
        result.append((start, (start[0] + k * d1[0], start[1] + k * d1[1]), (end[0] - k * d2[0], end[1] - k * d2[1]), end))
        start, t = end, t2
    return result


def parse_path(d):
    items = tokens(d)
    i = 0
    contours, current = [], []
    pos = start = (0.0, 0.0)
    last_ctrl = None
    last_cmd = ""
    command = None

    def number():
        nonlocal i
        value = float(items[i])
        i += 1
        return value

    def flag():
        nonlocal i
        token = items[i]
        if token in ("0", "1"):
            i += 1
            return int(token)
        items[i] = token[1:]
        return int(token[0])

    def close():
        nonlocal current
        if len(current) > 2:
            contours.append(current)
        current = []

    while i < len(items):
        if re.match(r"[A-Za-z]", items[i]):
            command = items[i]
            i += 1
            if command in "Zz":
                close()
                pos = start
                last_cmd = "Z"
                continue
        rel = command.islower()
        c = command.upper()
        ox, oy = pos if rel else (0.0, 0.0)
        if c == "M":
            close()
            pos = (ox + number(), oy + number())
            start = pos
            current = [pos]
            command = "l" if rel else "L"
            last_ctrl = None
        elif c == "L":
            pos = (ox + number(), oy + number())
            current.append(pos)
            last_ctrl = None
        elif c == "H":
            pos = ((pos[0] if rel else 0) + number(), pos[1])
            current.append(pos)
            last_ctrl = None
        elif c == "V":
            pos = (pos[0], (pos[1] if rel else 0) + number())
            current.append(pos)
            last_ctrl = None
        elif c in "CS":
            if c == "C":
                c1 = (ox + number(), oy + number())
            else:
                c1 = (2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]) if last_ctrl and last_cmd in "CS" else pos
            c2 = (ox + number(), oy + number())
            end = (ox + number(), oy + number())
            flatten_cubic(pos, c1, c2, end, current)
            last_ctrl, pos = c2, end
        elif c in "QT":
            if c == "Q":
                q = (ox + number(), oy + number())
            else:
                q = (2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]) if last_ctrl and last_cmd in "QT" else pos
            end = (ox + number(), oy + number())
            c1 = (pos[0] + 2 / 3 * (q[0] - pos[0]), pos[1] + 2 / 3 * (q[1] - pos[1]))
            c2 = (end[0] + 2 / 3 * (q[0] - end[0]), end[1] + 2 / 3 * (q[1] - end[1]))
            flatten_cubic(pos, c1, c2, end, current)
            last_ctrl, pos = q, end
        elif c == "A":
            rx, ry, angle = number(), number(), number()
            large, sweep = flag(), flag()
            end = (ox + number(), oy + number())
            for a, b, cc, dd in arc_to_cubics(pos, rx, ry, angle, large, sweep, end):
                flatten_cubic(a, b, cc, dd, current)
            pos = end
            last_ctrl = None
        else:
            raise ValueError(f"unsupported command {command}")
        last_cmd = c
    close()
    return contours


def load(name):
    text = (ROOT / "assets" / "stores" / f"{name}.svg").read_text()
    box = [float(v) for v in re.search(r'viewBox="([^"]+)"', text).group(1).replace(",", " ").split()]
    contours, even_odd = [], False
    for path in re.finditer(r"<path\b([^>]*)>", text):
        attributes = path.group(1)
        d = re.search(r'\sd="([^"]+)"', attributes).group(1)
        even_odd = even_odd or 'fill-rule="evenodd"' in attributes
        contours += parse_path(d)
    scale = 24 / max(box[2], box[3])
    dx = (24 - box[2] * scale) / 2 - box[0] * scale
    dy = (24 - box[3] * scale) / 2 - box[1] * scale
    contours = [[(x * scale + dx, y * scale + dy) for x, y in contour] for contour in contours]
    return contours, even_odd


def main():
    lines = ["// Generated by tools/store-marks.py from assets/stores/*.svg. Do not edit.",
             "// Sources and licenses: assets/stores/SOURCES.md and THIRD_PARTY_NOTICES.md."]
    entries = []
    for name in MARKS:
        contours, even_odd = load(name)
        points, ends = [], []
        for contour in contours:
            for x, y in contour:
                points.append(f"{x:.3f}f,{y:.3f}f")
            ends.append(str(len(points)))
        lines.append(f"constexpr float {name}Points[]={{")
        for at in range(0, len(points), 8):
            lines.append("    " + ",".join(points[at:at + 8]) + ",")
        lines.append("};")
        lines.append(f"constexpr std::uint16_t {name}Ends[]={{{','.join(ends)}}};")
        entries.append(f"{{{name}Points,{name}Ends,{len(ends)},{'true' if even_odd else 'false'}}}")
        print(f"{name}: {len(contours)} contours, {len(points)} points", file=sys.stderr)
    lines.append("constexpr MarkShape markShapes[]={" + ",".join(entries) + "};")
    (ROOT / "src" / "ui" / "store_marks.inc").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
