#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  スカイキューブ(手続き生成)
#
#  空のグラデーション + 太陽 + 雲を計算して、6 面のキューブマップを DDS で書き出す。
#  設計書: 設計書/Skybox設計.md
#
#  ------------------------------------------------------------------------
#  使い方
#
#      python3 Tools/SkyCubeGen/gen_skycube.py Game/Assets/Sky/SkyCube.dds 512
#
#  第 1 引数 = 出力パス、第 2 引数 = 1 面の解像度(省略時 512)。
#
#  **ビルドには組み込まない。** 生成物 (SkyCube.dds) をリポジトリへコミットし、
#  絵を変えたいときだけ手で叩く。理由:
#    - 毎ビルド走らせるには遅い (512 で数十秒)
#    - Python をビルドの必須依存にしたくない
#    - 生成物は 6MB 程度で、既存の Assets (92MB) に比べれば小さい
#
#  ------------------------------------------------------------------------
#  なぜ DDS なのか
#
#  aqEngine の Resource.cpp は DirectXTex の IsCubemap() を見て
#  Texture2DDesc::isCubemap を立てる。**DDS のキューブマップだけが、読み込み側に
#  新規実装なしでそのまま通る形式**。6 面バラの PNG にすると組み立てコードが要る。
#
#  ------------------------------------------------------------------------
#  絵を変えたいとき
#
#  下の「見た目のパラメータ」を触る。AquaDash のタイトル画面の空色に寄せてある。
#  市販の HDRI や自作画像へ差し替える場合は、DDS のキューブマップに変換して
#  同じパスへ置けばよい (このツールは不要になる)。
# ============================================================================
import math
import struct
import sys


# ----------------------------------------------------------------------------
#  見た目のパラメータ (0..1 のリニア値ではなく、そのまま 8bit へ書く sRGB 相当)
# ----------------------------------------------------------------------------

ZENITH  = (0.09, 0.16, 0.42)   # 天頂       : 濃紺
HORIZON = (0.62, 0.78, 0.92)   # 地平線付近 : 淡い水色
GROUND  = (0.34, 0.42, 0.50)   # 下方       : くすんだ青灰 (地面は別途描かれるのでほぼ見えない)

SUN_DIRECTION = (0.45, 0.35, 0.82)   # 太陽の向き (正規化はこの後で行う)
SUN_COLOR     = (1.00, 0.97, 0.82)

CLOUD_COVERAGE  = 0.48   # 大きいほど雲が減る (しきい値)
CLOUD_CONTRAST  = 3.0    # 大きいほど輪郭が硬くなる
CLOUD_STRENGTH  = 0.55   # 雲の白さの上限


def normalize(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return (v[0] / length, v[1] / length, v[2] / length)


SUN_DIRECTION = normalize(SUN_DIRECTION)


# ----------------------------------------------------------------------------
#  キューブ面の向き
#
#  D3D / Vulkan / Metal で共通の並び (+X, -X, +Y, -Y, +Z, -Z)。
#  u は右、v は**下**向き。ここを取り違えると面の継ぎ目で絵が飛ぶ。
# ----------------------------------------------------------------------------
def face_direction(face, u, v):
    if face == 0: return ( 1.0,   -v,   -u)   # +X
    if face == 1: return (-1.0,   -v,    u)   # -X
    if face == 2: return (   u,  1.0,    v)   # +Y
    if face == 3: return (   u, -1.0,   -v)   # -Y
    if face == 4: return (   u,   -v,  1.0)   # +Z
    return              (  -u,   -v, -1.0)    # -Z


# ----------------------------------------------------------------------------
#  値ノイズ
#
#  **ハッシュは整数の格子座標を取ること。** 実数をそのままハッシュすると
#  補間が効かず、雲ではなく per-texel のホワイトノイズ (フィルムグレイン) になる。
# ----------------------------------------------------------------------------
def lattice_hash(ix, iy, iz):
    n = ix * 374761393 + iy * 668265263 + iz * 2147483647
    n = (n ^ (n >> 13)) * 1274126177
    n = n ^ (n >> 16)
    return (n & 0xFFFFFF) / float(0xFFFFFF)


def smoothstep01(t):
    return t * t * (3.0 - 2.0 * t)


def value_noise(x, y, z):
    """格子点の乱数を smoothstep で三線形補間する"""
    fx0, fy0, fz0 = math.floor(x), math.floor(y), math.floor(z)
    tx, ty, tz = smoothstep01(x - fx0), smoothstep01(y - fy0), smoothstep01(z - fz0)
    ix, iy, iz = int(fx0), int(fy0), int(fz0)

    def corner(dx, dy, dz):
        return lattice_hash(ix + dx, iy + dy, iz + dz)

    x00 = corner(0, 0, 0) + (corner(1, 0, 0) - corner(0, 0, 0)) * tx
    x10 = corner(0, 1, 0) + (corner(1, 1, 0) - corner(0, 1, 0)) * tx
    x01 = corner(0, 0, 1) + (corner(1, 0, 1) - corner(0, 0, 1)) * tx
    x11 = corner(0, 1, 1) + (corner(1, 1, 1) - corner(0, 1, 1)) * tx
    y0 = x00 + (x10 - x00) * ty
    y1 = x01 + (x11 - x01) * ty
    return y0 + (y1 - y0) * tz


def cloud_amount(direction):
    """方向ベクトルから雲らしさ (0..1)。4 オクターブの fBm"""
    total, amplitude, frequency, weight = 0.0, 0.5, 2.5, 0.0
    for _ in range(4):
        total += amplitude * value_noise(direction[0] * frequency,
                                         direction[1] * frequency,
                                         direction[2] * frequency)
        weight += amplitude
        amplitude *= 0.5
        frequency *= 2.1
    value = total / weight
    return max(0.0, min(1.0, (value - CLOUD_COVERAGE) * CLOUD_CONTRAST))


def sky_color(direction):
    up = max(-1.0, min(1.0, direction[1]))

    # 1) 高さによるグラデーション
    if up >= 0.0:
        t = up ** 0.55
        color = tuple(HORIZON[i] + (ZENITH[i] - HORIZON[i]) * t for i in range(3))
    else:
        t = (-up) ** 0.7
        color = tuple(HORIZON[i] + (GROUND[i] - HORIZON[i]) * t for i in range(3))

    # 2) 太陽 (芯 + グロー + ハロー)
    cosine = max(0.0, direction[0] * SUN_DIRECTION[0]
                    + direction[1] * SUN_DIRECTION[1]
                    + direction[2] * SUN_DIRECTION[2])
    disc = 1.0 if cosine > 0.9995 else 0.0
    sun = min(1.0, disc + cosine ** 280.0 + 0.16 * (cosine ** 12.0))
    color = tuple(color[i] + (SUN_COLOR[i] - color[i]) * sun for i in range(3))

    # 3) 雲 (地平線寄りを厚く)
    if up > -0.05:
        band = min(1.0, max(0.0, 1.0 - abs(up) * 1.4))
        amount = cloud_amount(direction) * (0.25 + 0.75 * band)
        color = tuple(color[i] + (0.95 - color[i]) * amount * CLOUD_STRENGTH
                      for i in range(3))
    return color


# ----------------------------------------------------------------------------
#  DDS (レガシーヘッダ / BGRA8 / キューブマップ / ミップ無し)
# ----------------------------------------------------------------------------
def write_dds(path, size, faces):
    DDSD_CAPS   = 0x1
    DDSD_HEIGHT = 0x2
    DDSD_WIDTH  = 0x4
    DDSD_PITCH  = 0x8
    DDSD_FORMAT = 0x1000
    DDSCAPS_COMPLEX = 0x8
    DDSCAPS_TEXTURE = 0x1000
    DDSCAPS2_CUBEMAP_ALL = 0x200 | 0xFC00   # CUBEMAP | 6 面すべて
    DDPF_ALPHAPIXELS_RGB = 0x41

    header = bytearray(128)
    header[0:4] = b'DDS '
    struct.pack_into('<I', header, 4, 124)                       # dwSize
    struct.pack_into('<I', header, 8,
                     DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_FORMAT)
    struct.pack_into('<I', header, 12, size)                     # dwHeight
    struct.pack_into('<I', header, 16, size)                     # dwWidth
    struct.pack_into('<I', header, 20, size * 4)                 # dwPitchOrLinearSize
    struct.pack_into('<I', header, 28, 1)                        # dwMipMapCount
    struct.pack_into('<I', header, 76, 32)                       # ddspf.dwSize
    struct.pack_into('<I', header, 80, DDPF_ALPHAPIXELS_RGB)
    struct.pack_into('<I', header, 88, 32)                       # RGBBitCount
    struct.pack_into('<I', header, 92,  0x00FF0000)              # R (BGRA 並び)
    struct.pack_into('<I', header, 96,  0x0000FF00)              # G
    struct.pack_into('<I', header, 100, 0x000000FF)              # B
    struct.pack_into('<I', header, 104, 0xFF000000)              # A
    struct.pack_into('<I', header, 108, DDSCAPS_COMPLEX | DDSCAPS_TEXTURE)
    struct.pack_into('<I', header, 112, DDSCAPS2_CUBEMAP_ALL)

    with open(path, 'wb') as f:
        f.write(header)
        for face in faces:
            f.write(face)


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "usage: gen_skycube.py <out.dds> [size]", file=sys.stderr)
        return 2
    out_path = sys.argv[1]
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 512

    faces = []
    for face in range(6):
        buffer = bytearray(size * size * 4)
        offset = 0
        for y in range(size):
            v = (y + 0.5) / size * 2.0 - 1.0
            for x in range(size):
                u = (x + 0.5) / size * 2.0 - 1.0
                r, g, b = sky_color(normalize(face_direction(face, u, v)))
                buffer[offset]     = int(max(0.0, min(1.0, b)) * 255.0 + 0.5)
                buffer[offset + 1] = int(max(0.0, min(1.0, g)) * 255.0 + 0.5)
                buffer[offset + 2] = int(max(0.0, min(1.0, r)) * 255.0 + 0.5)
                buffer[offset + 3] = 255
                offset += 4
        faces.append(bytes(buffer))
        print(f"  面 {face + 1}/6 完了", file=sys.stderr)

    write_dds(out_path, size, faces)
    print(f"生成: {out_path} ({size}x{size} x6, {128 + size * size * 4 * 6} bytes)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
