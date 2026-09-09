# -*- coding: utf-8 -*-
"""AquaDash ステージサムネイル生成ツール。

.stage.json のコース定義から、タイトル画面のステージパネルに出す俯瞰サムネイル
  <StageId>_thumb.png … 512x288 (16:9) RGB 8bit
を生成する。依存ライブラリなし (標準ライブラリのみ)。

座標対応 (ミニマップと同じ真上構図):
  - 画像左上 (0,0) が -X / -Z 側。列 x → +X、行 y → +Z。上下反転なし。
  - 構図はコース XZ AABB + マージンを 16:9 へフィットさせる。はみ出す軸を基準に
    等倍スケールを決め、余った軸は中心合わせにするのでコースの縦横比は崩れない。
  - 描画は「背景の丘 → コースの帯 (白フチ + 路面色) → スタート/ゴールの点」の順。
    帯はコース中心線からのチャンファー距離場で塗るので、ループや交差でも切れない。

使い方:
  python Tools/generate_stage_thumbnail.py Assets/Stages/Stage01.stage.json
  (CWD = DirectX/Game。出力は Assets/UI/AquaDash/<StageId>_thumb.png)
"""

import json
import math
import os
import struct
import sys
import zlib

# ---- 構図 ----
IMAGE_WIDTH  = 512      # Title.screen.json の StageThumb は 208x117 (16:9)
IMAGE_HEIGHT = 288
MARGIN       = 40.0     # コース AABB の外側に取る余白 [m] (ミニマップの +40m に合わせる)
SAMPLE_STEP  = 1.0      # コース中心線を画素へ落とすときの弧長刻み [m]

# ---- コースの帯 ----
ROAD_MIN_HALF_PX = 3.0  # 路面半幅の最小値 [px] (長いコースでも線が消えないように)
BORDER_PX        = 1.3  # 路面の外側に足す白フチの幅 [px]
ROAD_RGB         = (0.30, 0.34, 0.42)   # CreateStageWorld の路面色と同じ青みグレー
BORDER_RGB       = (0.92, 0.96, 1.00)
ROAD_SHADOW_PX   = 2.6  # フチのさらに外側に落とす影の幅 [px]

# ---- 背景の地形 ----
GRASS_LO      = (0.16, 0.34, 0.14)   # 谷の緑
GRASS_HI      = (0.42, 0.62, 0.26)   # 峰の緑
NOISE_CELL    = 420.0   # 丘のうねりの基本波長 [m]
HILL_EXPONENT = 1.4     # ノイズの累乗 (>1 で谷を広く・峰を尖らせる)
LIGHT_DIR     = (-0.55, 0.83)        # 陰影を付ける光の XZ 方向 (正規化済み)
LIGHT_GAIN    = 2.2     # 傾斜から作る陰影の強さ
VIGNETTE      = 0.45    # 四隅の暗さ (0=なし)
CORRIDOR_M    = 55.0    # コース沿いを少し明るくする帯の半幅 [m]
CORRIDOR_GAIN = 0.18    # その帯の明るさの上乗せ量
SEED          = 5150

# ---- マーカー ----
START_RGB    = (0.25, 0.95, 1.00)
GOAL_RGB     = (1.00, 0.28, 0.24)
MARKER_R_PX  = 4.5      # マーカーの半径 [px]
MARKER_RING  = 1.6      # マーカーの白フチの幅 [px]


# ---------------------------------------------------------------- スプライン

def catmull_rom(p0, p1, p2, p3, t):
    t2 = t * t
    t3 = t2 * t
    return tuple(
        0.5 * ((2.0 * p1[i]) + (-p0[i] + p2[i]) * t
               + (2.0 * p0[i] - 5.0 * p1[i] + 4.0 * p2[i] - p3[i]) * t2
               + (-p0[i] + 3.0 * p1[i] - 3.0 * p2[i] + p3[i]) * t3)
        for i in range(3))


def sample_spline(points, per_segment=64):
    """端点クランプの一様 Catmull-Rom を全区間サンプリングし (位置列, 累積弧長列) を返す。"""
    n = len(points)
    clamp = lambda i: points[max(0, min(n - 1, i))]
    positions = []
    for seg in range(n - 1):
        last = per_segment if seg == n - 2 else per_segment - 1
        for s in range(last + 1):
            t = s / per_segment
            positions.append(catmull_rom(clamp(seg - 1), clamp(seg), clamp(seg + 1), clamp(seg + 2), t))
    lengths = [0.0]
    for i in range(1, len(positions)):
        lengths.append(lengths[-1] + math.dist(positions[i - 1], positions[i]))
    return positions, lengths


def evaluate_at(positions, lengths, d):
    """弧長 d の位置を線形補間で返す (エンジンの弧長テーブルと同じ考え方)。"""
    if d <= 0.0:
        return positions[0]
    if d >= lengths[-1]:
        return positions[-1]
    lo, hi = 0, len(lengths) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if lengths[mid] <= d:
            lo = mid
        else:
            hi = mid
    seg = lengths[hi] - lengths[lo]
    t = 0.0 if seg <= 0.0 else (d - lengths[lo]) / seg
    a, b = positions[lo], positions[hi]
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


# ---------------------------------------------------------------- ノイズ

def _hash01(ix, iz):
    h = (ix * 374761393 + iz * 668265263 + SEED * 2246822519) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def _value_noise(x, z):
    ix, iz = math.floor(x), math.floor(z)
    fx, fz = x - ix, z - iz
    sx = fx * fx * (3.0 - 2.0 * fx)
    sz = fz * fz * (3.0 - 2.0 * fz)
    v00 = _hash01(ix, iz)
    v10 = _hash01(ix + 1, iz)
    v01 = _hash01(ix, iz + 1)
    v11 = _hash01(ix + 1, iz + 1)
    return (v00 * (1 - sx) + v10 * sx) * (1 - sz) + (v01 * (1 - sx) + v11 * sx) * sz


def fbm(x, z, octaves=4):
    total, amp, freq, norm = 0.0, 1.0, 1.0, 0.0
    for _ in range(octaves):
        total += _value_noise(x * freq, z * freq) * amp
        norm += amp
        amp *= 0.5
        freq *= 2.0
    return total / norm


def smoothstep(lo, hi, v):
    if v <= lo:
        return 0.0
    if v >= hi:
        return 1.0
    t = (v - lo) / (hi - lo)
    return t * t * (3.0 - 2.0 * t)


def lerp3(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


# ---------------------------------------------------------------- 距離場

def chamfer_distance(seeds, width, height):
    """コース画素からの近似距離 [px] を返す (2 パス チャンファー法。斜め=1.4)。"""
    inf = 1.0e9
    dist = [inf] * (width * height)
    for (px, pz) in seeds:
        dist[pz * width + px] = 0.0

    def relax(i, j, add):
        d = dist[j] + add
        if d < dist[i]:
            dist[i] = d

    for z in range(height):
        for x in range(width):
            i = z * width + x
            if x > 0:
                relax(i, i - 1, 1.0)
            if z > 0:
                relax(i, i - width, 1.0)
                if x > 0:
                    relax(i, i - width - 1, 1.4)
                if x < width - 1:
                    relax(i, i - width + 1, 1.4)
    for z in range(height - 1, -1, -1):
        for x in range(width - 1, -1, -1):
            i = z * width + x
            if x < width - 1:
                relax(i, i + 1, 1.0)
            if z < height - 1:
                relax(i, i + width, 1.0)
                if x < width - 1:
                    relax(i, i + width + 1, 1.4)
                if x > 0:
                    relax(i, i + width - 1, 1.4)
    return dist


# ---------------------------------------------------------------- PNG 出力

def write_png_rgb(path, width, height, rgb_rows):
    """RGB 8bit PNG を書く (filter 0 / zlib)。rgb_rows は行ごとの bytes。"""
    raw = b"".join(b"\x00" + row for row in rgb_rows)

    def chunk(tag, payload):
        data = tag + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)   # 8bit / RGB
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------- メイン

def main():
    if len(sys.argv) < 2:
        print("usage: python generate_stage_thumbnail.py <path/to/StageXX.stage.json>")
        return 1
    stage_path = sys.argv[1]
    stage_id = os.path.basename(stage_path).split(".")[0]
    out_dir = os.path.normpath(os.path.join(os.path.dirname(stage_path), "..", "UI", "AquaDash"))

    with open(stage_path, encoding="utf-8") as f:
        stage = json.load(f)
    course = stage["course"]
    points = [tuple(p["position"]) for p in course["points"]]
    road_width = float(course.get("width", 12.0))

    positions, lengths = sample_spline(points)
    total = lengths[-1]

    # コース XZ AABB + マージンを 16:9 へフィットさせる (はみ出す軸を基準に等倍・中心合わせ)。
    xs = [p[0] for p in positions]
    zs = [p[2] for p in positions]
    min_x, max_x = min(xs) - MARGIN, max(xs) + MARGIN
    min_z, max_z = min(zs) - MARGIN, max(zs) + MARGIN
    px_per_m = min(IMAGE_WIDTH / (max_x - min_x), IMAGE_HEIGHT / (max_z - min_z))
    center_x = (min_x + max_x) * 0.5
    center_z = (min_z + max_z) * 0.5
    origin_x = center_x - IMAGE_WIDTH * 0.5 / px_per_m
    origin_z = center_z - IMAGE_HEIGHT * 0.5 / px_per_m
    m_per_px = 1.0 / px_per_m
    print(f"{stage_id}: total={total:.1f}m origin=({origin_x:.1f},{origin_z:.1f}) "
          f"{m_per_px:.2f} m/px")

    def to_px(p):
        return ((p[0] - origin_x) * px_per_m, (p[2] - origin_z) * px_per_m)

    # コース中心線を画素へ落として距離場を作る。
    seeds = set()
    d = 0.0
    while d <= total:
        fx, fz = to_px(evaluate_at(positions, lengths, d))
        px, pz = int(round(fx)), int(round(fz))
        if 0 <= px < IMAGE_WIDTH and 0 <= pz < IMAGE_HEIGHT:
            seeds.add((px, pz))
        d += SAMPLE_STEP
    dist_px = chamfer_distance(seeds, IMAGE_WIDTH, IMAGE_HEIGHT)

    road_half = max(ROAD_MIN_HALF_PX, road_width * 0.5 * px_per_m)
    border_hi = road_half + BORDER_PX
    shadow_hi = border_hi + ROAD_SHADOW_PX

    # 背景の丘。高さノイズの中心差分から擬似ランバートの陰影を作る。
    def hill(wx, wz):
        return fbm(wx / NOISE_CELL, wz / NOISE_CELL) ** HILL_EXPONENT

    half_w = IMAGE_WIDTH * 0.5
    half_h = IMAGE_HEIGHT * 0.5
    rows = []
    for z in range(IMAGE_HEIGHT):
        row = bytearray()
        wz = origin_z + (z + 0.5) * m_per_px
        for x in range(IMAGE_WIDTH):
            i = z * IMAGE_WIDTH + x
            wx = origin_x + (x + 0.5) * m_per_px

            h = hill(wx, wz)
            dx = (hill(wx + m_per_px, wz) - hill(wx - m_per_px, wz)) * 0.5
            dz = (hill(wx, wz + m_per_px) - hill(wx, wz - m_per_px)) * 0.5
            shade = 1.0 + LIGHT_GAIN * (dx * LIGHT_DIR[0] + dz * LIGHT_DIR[1])
            shade = max(0.55, min(1.35, shade))

            color = lerp3(GRASS_LO, GRASS_HI, h)
            # コース沿いは少し明るくして、帯だけが浮いて見えないようにする。
            corridor = 1.0 - smoothstep(0.0, CORRIDOR_M, dist_px[i] * m_per_px)
            shade *= 1.0 + CORRIDOR_GAIN * corridor
            # 四隅を落として中央のコースへ視線を寄せる。
            r = math.hypot((x - half_w) / half_w, (z - half_h) / half_h)
            shade *= 1.0 - VIGNETTE * smoothstep(0.55, 1.35, r)
            color = tuple(c * shade for c in color)

            # コースの帯 (外側から 影 → 白フチ → 路面)。
            dp = dist_px[i]
            if dp < shadow_hi:
                if dp < road_half:
                    color = ROAD_RGB
                elif dp < border_hi:
                    color = BORDER_RGB
                else:
                    t = smoothstep(border_hi, shadow_hi, dp)
                    color = lerp3(tuple(c * 0.45 for c in color), color, t)

            row += bytes(min(255, max(0, round(c * 255.0))) for c in color)
        rows.append(row)

    # スタート / ゴールのマーカー (ゴール距離はコース長でクランプする)。
    goal_distance = float(stage.get("goal", {}).get("distance", total))
    markers = [
        (to_px(evaluate_at(positions, lengths, 0.0)), START_RGB),
        (to_px(evaluate_at(positions, lengths, min(goal_distance, total))), GOAL_RGB),
    ]
    ring_r = MARKER_R_PX + MARKER_RING
    for (cx, cz), rgb in markers:
        x0, x1 = int(cx - ring_r) - 1, int(cx + ring_r) + 1
        z0, z1 = int(cz - ring_r) - 1, int(cz + ring_r) + 1
        for z in range(max(0, z0), min(IMAGE_HEIGHT, z1 + 1)):
            for x in range(max(0, x0), min(IMAGE_WIDTH, x1 + 1)):
                r = math.hypot(x + 0.5 - cx, z + 0.5 - cz)
                if r > ring_r:
                    continue
                c = rgb if r <= MARKER_R_PX else BORDER_RGB
                rows[z][x * 3:x * 3 + 3] = bytes(min(255, round(v * 255.0)) for v in c)

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{stage_id}_thumb.png")
    write_png_rgb(out_path, IMAGE_WIDTH, IMAGE_HEIGHT, [bytes(r) for r in rows])
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
