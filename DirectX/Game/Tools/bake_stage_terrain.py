# -*- coding: utf-8 -*-
"""AquaDash ステージ地形ベイクツール。

.stage.json のコース定義から、HeightmapChunk 用の
  <StageId>_heightmap.png … R チャンネル 8bit (高さ = R/255 * heightScale)
  <StageId>_splatmap.png  … R=grass / G=snow / B=rock の混合比
を生成する。依存ライブラリなし (標準ライブラリのみ)。

座標対応 (エンジン仕様。カリング設計.md ではなく HeightmapChunk.cpp が一次資料):
  - 画像左上 (0,0) = 地形原点 (originX, originZ)。列 x → +X、行 y → +Z。上下反転なし。
  - heightmap は端ピクセル = 端頂点 (worldX = originX + terrainSize * px/(W-1))。
  - 地形原点 / terrainSize は CreateStageWorld (AquaDashStates.cpp) と同一式:
      コース XZ AABB (スプラインを弧長 10m 刻みでサンプリング、min/max は 0 初期化)
      + マージン 60m、terrainSize = max(extentX, extentZ)。
  - コース回廊 (中心線 ±FLAT_RADIUS) は R=0 の完全平坦にして走行系と整合させる。

使い方:
  python bake_stage_terrain.py ../Assets/Stages/Stage01.stage.json
  (出力は ../Assets/Terrain/<StageId>_heightmap.png / _splatmap.png)
"""

import json
import math
import os
import struct
import sys
import zlib

# ---- ベイクパラメータ (設計書 05_実装フェーズ計画.md P11) ----
IMAGE_SIZE    = 512     # heightmap / splatmap とも 512x512
MARGIN        = 60.0    # CreateStageWorld と同じ地形マージン [m]
SAMPLE_STEP   = 10.0    # CreateStageWorld と同じ AABB サンプリング間隔 [m]
FLAT_RADIUS   = 35.0    # コース回廊 (完全平坦) の半幅 [m]
RAMP_LENGTH   = 80.0    # 回廊端から丘への立ち上がり距離 [m]
HEIGHT_SCALE  = 22.0    # 丘の最大高さ [m] (.stage.json の terrain.heightScale と一致させる)
NOISE_CELL    = 420.0   # 丘のうねりの基本波長 [m] (小さくすると砂嵐状になる)
HILL_EXPONENT = 1.6     # ノイズの累乗 (>1 で谷を広く・峰を尖らせる)
BLUR_RADIUS   = 2       # 8bit バンディング抑制のガウスぼかし半径 [px]
ROCK_SLOPE_LO = 0.10    # 岩が混ざり始める勾配 (dh/dx)
ROCK_SLOPE_HI = 0.32    # 岩が支配的になる勾配
SNOW_H_LO     = 0.55    # 雪が現れ始める正規化高さ
SNOW_H_HI     = 0.80    # 雪が支配的になる正規化高さ
SEED          = 1234


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
        a, b = positions[i - 1], positions[i]
        lengths.append(lengths[-1] + math.dist(a, b))
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


def fbm(x, z, octaves=3):
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


# ---------------------------------------------------------------- 距離場 / ぼかし

def chamfer_distance(seeds, size):
    """コース画素からの近似距離 [px] (2 パス チャンファー法。斜め=1.4)。"""
    inf = 1.0e9
    dist = [inf] * (size * size)
    for (px, pz) in seeds:
        dist[pz * size + px] = 0.0
    for z in range(size):
        for x in range(size):
            i = z * size + x
            d = dist[i]
            if x > 0:
                d = min(d, dist[i - 1] + 1.0)
            if z > 0:
                d = min(d, dist[i - size] + 1.0)
                if x > 0:
                    d = min(d, dist[i - size - 1] + 1.4)
                if x < size - 1:
                    d = min(d, dist[i - size + 1] + 1.4)
            dist[i] = d
    for z in range(size - 1, -1, -1):
        for x in range(size - 1, -1, -1):
            i = z * size + x
            d = dist[i]
            if x < size - 1:
                d = min(d, dist[i + 1] + 1.0)
            if z < size - 1:
                d = min(d, dist[i + size] + 1.0)
                if x < size - 1:
                    d = min(d, dist[i + size + 1] + 1.4)
                if x > 0:
                    d = min(d, dist[i + size - 1] + 1.4)
            dist[i] = d
    return dist


def gaussian_blur(values, size, radius):
    """分離ガウスぼかし (値は 0..1 のリスト)。"""
    sigma = max(0.5, radius * 0.6)
    kernel = [math.exp(-(k * k) / (2.0 * sigma * sigma)) for k in range(-radius, radius + 1)]
    ksum = sum(kernel)
    kernel = [k / ksum for k in kernel]

    tmp = [0.0] * (size * size)
    for z in range(size):
        row = z * size
        for x in range(size):
            acc = 0.0
            for k in range(-radius, radius + 1):
                xx = min(size - 1, max(0, x + k))
                acc += values[row + xx] * kernel[k + radius]
            tmp[row + x] = acc
    out = [0.0] * (size * size)
    for z in range(size):
        for x in range(size):
            acc = 0.0
            for k in range(-radius, radius + 1):
                zz = min(size - 1, max(0, z + k))
                acc += tmp[zz * size + x] * kernel[k + radius]
            out[z * size + x] = acc
    return out


# ---------------------------------------------------------------- PNG 出力

def write_png_rgb(path, size, rgb_rows):
    """RGB 8bit PNG を書く (filter 0 / zlib)。rgb_rows は行ごとの bytes。"""
    raw = b"".join(b"\x00" + row for row in rgb_rows)

    def chunk(tag, payload):
        data = tag + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)   # 8bit / RGB
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------- メイン

def main():
    if len(sys.argv) < 2:
        print("usage: python bake_stage_terrain.py <path/to/StageXX.stage.json>")
        return 1
    stage_path = sys.argv[1]
    stage_id = os.path.basename(stage_path).split(".")[0]
    out_dir = os.path.normpath(os.path.join(os.path.dirname(stage_path), "..", "Terrain"))

    with open(stage_path, encoding="utf-8") as f:
        stage = json.load(f)
    points = [tuple(p["position"]) for p in stage["course"]["points"]]

    positions, lengths = sample_spline(points)
    total = lengths[-1]

    # 地形原点 / terrainSize (CreateStageWorld と同一式)。
    min_x = max_x = min_z = max_z = 0.0
    d = 0.0
    while d <= total:
        p = evaluate_at(positions, lengths, d)
        min_x, max_x = min(min_x, p[0]), max(max_x, p[0])
        min_z, max_z = min(min_z, p[2]), max(max_z, p[2])
        d += SAMPLE_STEP
    extent_x = max_x - min_x + MARGIN * 2.0
    extent_z = max_z - min_z + MARGIN * 2.0
    terrain_size = max(extent_x, extent_z)
    origin_x, origin_z = min_x - MARGIN, min_z - MARGIN
    px_per_m = (IMAGE_SIZE - 1) / terrain_size
    print(f"{stage_id}: total={total:.1f}m origin=({origin_x:.1f},{origin_z:.1f}) size={terrain_size:.1f}m "
          f"({1.0 / px_per_m:.2f} m/px)")

    # コース中心線を 1m 刻みで画素へ落とし、距離場を作る。
    seeds = set()
    d = 0.0
    while d <= total:
        p = evaluate_at(positions, lengths, d)
        px = round((p[0] - origin_x) * px_per_m)
        pz = round((p[2] - origin_z) * px_per_m)
        if 0 <= px < IMAGE_SIZE and 0 <= pz < IMAGE_SIZE:
            seeds.add((px, pz))
        d += 1.0
    dist_px = chamfer_distance(sorted(seeds), IMAGE_SIZE)

    # 高さ: 回廊は 0、外側は smoothstep 立ち上げ x バリューノイズの丘。
    height = [0.0] * (IMAGE_SIZE * IMAGE_SIZE)
    for z in range(IMAGE_SIZE):
        wz = origin_z + z / px_per_m
        for x in range(IMAGE_SIZE):
            d_m = dist_px[z * IMAGE_SIZE + x] / px_per_m
            ramp = smoothstep(FLAT_RADIUS, FLAT_RADIUS + RAMP_LENGTH, d_m)
            if ramp <= 0.0:
                continue
            wx = origin_x + x / px_per_m
            n = fbm(wx / NOISE_CELL, wz / NOISE_CELL)
            height[z * IMAGE_SIZE + x] = ramp * (n ** HILL_EXPONENT)

    height = gaussian_blur(height, IMAGE_SIZE, BLUR_RADIUS)
    # ぼかしで回廊へ滲んだ分を切り戻す (立ち上がりが 0 始まりなので段差は出ない)。
    for i in range(IMAGE_SIZE * IMAGE_SIZE):
        if dist_px[i] / px_per_m < FLAT_RADIUS:
            height[i] = 0.0

    # スプラット: 平地=草 (低周波ノイズで岩を少し混ぜる)、斜面=岩、高所=雪。
    m_per_px = 1.0 / px_per_m
    splat_rows = []
    height_rows = []
    for z in range(IMAGE_SIZE):
        srow = bytearray()
        hrow = bytearray()
        wz = origin_z + z * m_per_px
        for x in range(IMAGE_SIZE):
            i = z * IMAGE_SIZE + x
            h = height[i]
            hv = min(255, max(0, round(h * 255.0)))
            hrow += bytes((hv, hv, hv))

            xl = height[i - 1] if x > 0 else h
            xr = height[i + 1] if x < IMAGE_SIZE - 1 else h
            zu = height[i - IMAGE_SIZE] if z > 0 else h
            zd = height[i + IMAGE_SIZE] if z < IMAGE_SIZE - 1 else h
            slope = math.hypot((xr - xl) * HEIGHT_SCALE / (2.0 * m_per_px),
                               (zd - zu) * HEIGHT_SCALE / (2.0 * m_per_px))
            wx = origin_x + x * m_per_px
            # 勾配ベースの岩は、コース縁の立ち上がり帯では出さず (均一な帯になって不自然)、
            # ランプの外の丘斜面だけに、ノイズでまだらに割って乗せる。
            d_m = dist_px[i] / px_per_m
            ring_fade = smoothstep(FLAT_RADIUS + RAMP_LENGTH, FLAT_RADIUS + RAMP_LENGTH + 60.0, d_m)
            patchy = 0.35 + 0.65 * fbm(wx / 150.0, wz / 150.0, 2)
            rock = smoothstep(ROCK_SLOPE_LO, ROCK_SLOPE_HI, slope) * patchy * ring_fade
            snow = smoothstep(SNOW_H_LO, SNOW_H_HI, h) * (1.0 - 0.6 * rock)
            if h < 0.05:
                rock = max(rock, 0.15 * fbm(wx / 47.0, wz / 47.0, 2))
            grass = max(0.0, 1.0 - rock - snow)
            srow += bytes((min(255, round(grass * 255.0)),
                           min(255, round(snow * 255.0)),
                           min(255, round(rock * 255.0))))
        splat_rows.append(bytes(srow))
        height_rows.append(bytes(hrow))

    # 外周 2px はエッジ複製 (splat サンプラーが Wrap のため反対側と混ざらないように)。
    def pad_edges(rows):
        rows[0] = rows[2]
        rows[1] = rows[2]
        rows[-1] = rows[-3]
        rows[-2] = rows[-3]
        out = []
        for row in rows:
            b = bytearray(row)
            b[0:3] = b[6:9]
            b[3:6] = b[6:9]
            b[-3:] = b[-9:-6]
            b[-6:-3] = b[-9:-6]
            out.append(bytes(b))
        return out

    splat_rows = pad_edges(splat_rows)

    os.makedirs(out_dir, exist_ok=True)
    hpath = os.path.join(out_dir, f"{stage_id}_heightmap.png")
    spath = os.path.join(out_dir, f"{stage_id}_splatmap.png")
    write_png_rgb(hpath, IMAGE_SIZE, height_rows)
    write_png_rgb(spath, IMAGE_SIZE, splat_rows)
    print(f"wrote {hpath}")
    print(f"wrote {spath}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
