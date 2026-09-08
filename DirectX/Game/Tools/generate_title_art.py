# -*- coding: utf-8 -*-
"""タイトル背景アートの生成ツール。

GREEN COAST のテーマ(青空 + 太陽 + 海 + 緑の海岸丘)をフラットデザインで合成し、
Assets/UI/AquaDash/TitleBG.png (1920x1080, RGB) へ出力する。
タイトル画面の青系パレット(Title.screen.json)に合わせている。
依存ライブラリなし (標準ライブラリのみ)。

使い方:
  python generate_title_art.py          (出力: ../Assets/UI/AquaDash/TitleBG.png)
"""

import math
import os
import random
import struct
import sys
import zlib

W, H     = 1920, 1080
HORIZON  = 640                 # 海と空の境界 [px]
SUN      = (1380.0, 430.0)     # 太陽の中心
SUN_R    = 110.0
SEED     = 7


def lerp(a, b, t):
    return a + (b - a) * t


def lerp3(c0, c1, t):
    return (lerp(c0[0], c1[0], t), lerp(c0[1], c1[1], t), lerp(c0[2], c1[2], t))


def over(base, top, alpha):
    return (lerp(base[0], top[0], alpha), lerp(base[1], top[1], alpha), lerp(base[2], top[2], alpha))


def smoothstep(lo, hi, v):
    if v <= lo:
        return 0.0
    if v >= hi:
        return 1.0
    t = (v - lo) / (hi - lo)
    return t * t * (3.0 - 2.0 * t)


def bumps(x, spec):
    """(中心, 半幅, 高さ) のコサインバンプ列の最大値 [px]。丘のシルエット用。"""
    h = 0.0
    for cx, hw, amp in spec:
        d = abs(x - cx)
        if d < hw:
            h = max(h, amp * 0.5 * (1.0 + math.cos(math.pi * d / hw)))
    return h


# 丘 (左が海岸の緑) 。奥→手前の 2 層。
FAR_HILLS  = [(120, 620, 150), (620, 520, 110), (1050, 420, 60)]
NEAR_HILLS = [(-80, 560, 260), (430, 470, 190), (820, 330, 90)]

# 雲 (中心x, 中心y, 半幅, 半高)
CLOUDS = [(330, 170, 190, 30), (760, 110, 140, 22), (1090, 240, 110, 18), (1660, 150, 150, 24)]


def write_png_rgb(path, width, height, rows):
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(tag, payload):
        data = tag + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "UI", "AquaDash", "TitleBG.png"))

    rng = random.Random(SEED)

    # 海のきらめき (水平ダッシュ)。太陽の直下ほど濃く。
    sparkles = []
    for _ in range(240):
        y = rng.randint(HORIZON + 8, H - 40)
        depth = (y - HORIZON) / (H - HORIZON)
        x = rng.randint(0, W - 1)
        length = rng.randint(10, 30 + int(60 * depth))
        near_sun = math.exp(-((x - SUN[0]) / 420.0) ** 2)
        alpha = (0.10 + 0.35 * near_sun) * (1.0 - 0.5 * depth)
        sparkles.append((x, y, length, alpha))
    sparkle_rows = {}
    for x, y, length, alpha in sparkles:
        sparkle_rows.setdefault(y, []).append((x, x + length, alpha))

    far_h  = [bumps(x, FAR_HILLS) for x in range(W)]
    near_h = [bumps(x, NEAR_HILLS) for x in range(W)]

    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            if y < HORIZON:
                # 空: 上が濃紺、地平線際が明るいシアン。
                t = y / HORIZON
                c = lerp3((8, 30, 74), (125, 205, 238), t ** 1.25)
                # 太陽 (本体 + グロー)
                d = math.hypot(x - SUN[0], y - SUN[1])
                if d < SUN_R:
                    edge = smoothstep(SUN_R - 3.0, SUN_R, d)
                    c = over((255, 244, 198), c, edge)
                else:
                    glow = math.exp(-((d - SUN_R) / 170.0) ** 2) * 0.18
                    c = over(c, (255, 236, 180), glow)
                # 雲 (フラットな楕円)
                for cx, cy, hw, hh in CLOUDS:
                    e = ((x - cx) / hw) ** 2 + ((y - cy) / hh) ** 2
                    if e < 1.0:
                        c = over(c, (228, 243, 252), 0.8 * (1.0 - smoothstep(0.7, 1.0, e)))
            else:
                # 海: 地平線際が明るく、手前ほど深い青。
                t = (y - HORIZON) / (H - HORIZON)
                c = lerp3((22, 128, 178), (8, 56, 100), t ** 0.8)
                # 太陽の反射柱
                near_sun = math.exp(-((x - SUN[0]) / (140.0 + 500.0 * t)) ** 2)
                c = over(c, (255, 230, 170), 0.18 * near_sun * (1.0 - t))
                # きらめき
                for x0, x1, alpha in sparkle_rows.get(y, ()):
                    if x0 <= x <= x1:
                        c = over(c, (235, 250, 255), alpha)
                        break

            # 海岸の丘 (左) 。地平線の上にシルエットとして乗せ、裾は海へ少しだけ食い込ませる
            # (地平線をまたぐ隙間を消すため)。奥の層 → 手前の層の順に上書き。
            fh = far_h[x]
            nh = near_h[x]
            if fh > 2.0 and HORIZON - fh < y < HORIZON + 8:
                c = (40, 126, 86)
            if nh > 2.0 and HORIZON - nh < y < HORIZON + 14:
                c = (26, 100, 64)
                # 手前の丘と海の境に砂浜の縁取り
                if y >= HORIZON + 8:
                    c = (226, 206, 148)

            row += bytes((int(max(0, min(255, c[0]))),
                          int(max(0, min(255, c[1]))),
                          int(max(0, min(255, c[2])))))
        rows.append(bytes(row))

    write_png_rgb(out_path, W, H, rows)
    print(f"wrote {out_path} ({W}x{H})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
