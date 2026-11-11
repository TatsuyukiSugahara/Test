#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  IBL 用テクスチャ生成(irradiance キューブ / 前置フィルタ済み鏡面キューブ / BRDF LUT)
#
#  スカイキューブ DDS を読み、split-sum 近似(Karis 2013)の 3 本を DDS で書き出す。
#  設計書: 設計書/IBL設計.md(§2 がこのツールの仕様)
#
#  ------------------------------------------------------------------------
#  使い方
#
#      python3 Tools/IBLGen/gen_ibl.py aqEngine/Assets/Sky/DefaultSkyCube.dds aqEngine/Assets/Sky/DefaultSkyCube
#          → DefaultSkyCube_Irradiance.dds / DefaultSkyCube_Specular.dds を出力
#      python3 Tools/IBLGen/gen_ibl.py --lut aqEngine/Assets/Sky/BRDF_LUT.dds
#          → 空に依存しない LUT。1 回作れば空を差し替えても作り直さない
#      python3 Tools/IBLGen/gen_ibl.py --selftest
#          → 設計書 §2.5 の自己テスト
#      python3 Tools/IBLGen/gen_ibl.py --verify aqEngine/Assets/Sky/DefaultSkyCube.dds aqEngine/Assets/Sky/DefaultSkyCube \
#                                      --lut aqEngine/Assets/Sky/BRDF_LUT.dds --png <出力フォルダ>
#          → 生成物を実データで検証する(設計書 §4 P1 の自動評価)。--png で確認用 PNG も書く
#
#  共通オプション: --jobs N(並列数。省略時は CPU 数)、--png DIR(確認用 PNG の出力先)
#
#  **ビルドには組み込まない。** 生成物をリポジトリへコミットし、空を差し替えたら手で叩く
#  (gen_skycube.py と同じ理由)。標準ライブラリのみで動く(numpy は使わない)。
#
#  ------------------------------------------------------------------------
#  生成物(3 本とも DX10 ヘッダ / DXGI_FORMAT_R16G16B16A16_FLOAT)
#
#    <prefix>_Irradiance.dds : 32² × 6 面 / ミップ 1。値は E / π(一様な空 L で L が戻る)
#    <prefix>_Specular.dds   : 128² × 6 面 / ミップ 6(128..4)。mip k は roughness = k / 5
#    BRDF_LUT.dds            : 128² の 2D / ミップ 1。u = NdotV、v = roughness(行 0 が最小)、rg = (A, B)
#
#  入力の値は変換せずそのまま積分する(sRGB → linear をしない。Skybox.fx が DDS の値を
#  そのまま出しているので、映り込みと見えている空を同じ解釈にする)。
# ============================================================================
import argparse
import importlib.util
import math
import multiprocessing
import os
import struct
import sys
import tempfile
import time
import zlib
from array import array


# ----------------------------------------------------------------------------
#  生成パラメータ(設計書 §0.2 / §2)
# ----------------------------------------------------------------------------

SPECULAR_SIZE     = 128   # 鏡面キューブ mip 0 の 1 面の解像度
SPECULAR_MIPS     = 6     # 128, 64, 32, 16, 8, 4
SPECULAR_SAMPLES  = 256   # 鏡面の重点サンプリング数(1 テクセルあたり)
IRRADIANCE_SIZE   = 32    # irradiance キューブの 1 面の解像度
SH_SOURCE_SIZE    = 64    # SH 射影に使う入力ミップの解像度
LUT_SIZE          = 128
LUT_SAMPLES       = 512
MIN_CHAIN_SIZE    = 4     # 入力から作るミップ列の最小解像度
ALPHA_MIN         = 0.001 # PBRFunctions.fx の EvalDisneyBRDF と同じ下限

DXGI_FORMAT_R16G16B16A16_FLOAT = 10
DXGI_FORMAT_R8G8B8A8_UNORM      = 28
DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29
DXGI_FORMAT_B8G8R8A8_UNORM      = 87
DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91


# ----------------------------------------------------------------------------
#  キューブ面の向き(gen_skycube.py の face_direction をそのまま写したもの)
#
#  D3D / Vulkan / Metal で共通の並び (+X, -X, +Y, -Y, +Z, -Z)。
#  u は右、v は**下**向き。ここを取り違えると面の継ぎ目で絵が飛ぶ。
#  --selftest で gen_skycube.py の実体と突き合わせている(独自に書き換えないこと)。
# ----------------------------------------------------------------------------
def face_direction(face, u, v):
    if face == 0: return ( 1.0,   -v,   -u)   # +X
    if face == 1: return (-1.0,   -v,    u)   # -X
    if face == 2: return (   u,  1.0,    v)   # +Y
    if face == 3: return (   u, -1.0,   -v)   # -Y
    if face == 4: return (   u,   -v,  1.0)   # +Z
    return              (  -u,   -v, -1.0)    # -Z


def direction_to_face_uv(x, y, z):
    """face_direction の逆写像。方向 → (面, u, v)。u, v は [-1, 1]"""
    ax, ay, az = abs(x), abs(y), abs(z)
    if ax >= ay and ax >= az:
        if x > 0.0:
            return 0, -z / ax, -y / ax
        return 1, z / ax, -y / ax
    if ay >= az:
        if y > 0.0:
            return 2, x / ay, z / ay
        return 3, x / ay, -z / ay
    if z > 0.0:
        return 4, x / az, -y / az
    return 5, -x / az, -y / az


def normalize(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return (v[0] / length, v[1] / length, v[2] / length)


def texel_uv(x, y, size):
    return (x + 0.5) / size * 2.0 - 1.0, (y + 0.5) / size * 2.0 - 1.0


def texel_direction(face, x, y, size):
    u, v = texel_uv(x, y, size)
    return normalize(face_direction(face, u, v))


# ----------------------------------------------------------------------------
#  画像の表現
#
#  1 面 = array('f')(RGB を行優先で詰めたもの。長さ size * size * 3)。
#  ミップ列 chain = [(size, [面 0..5]), ...](先頭が最大解像度)。
#  array にしているのは、multiprocessing のワーカーへ渡すときの pickle を軽くするため。
# ----------------------------------------------------------------------------
def make_face(size, fill=None):
    if fill is None:
        return array('f', bytes(size * size * 3 * 4))
    return array('f', list(fill) * (size * size))


def downsample(face, size):
    """2×2 平均で 1 段縮小する"""
    half = size // 2
    out = make_face(half)
    stride = size * 3
    for y in range(half):
        row0 = 2 * y * stride
        row1 = row0 + stride
        o = y * half * 3
        for x in range(half):
            i0 = row0 + 6 * x
            i1 = row1 + 6 * x
            out[o]     = (face[i0]     + face[i0 + 3] + face[i1]     + face[i1 + 3]) * 0.25
            out[o + 1] = (face[i0 + 1] + face[i0 + 4] + face[i1 + 1] + face[i1 + 4]) * 0.25
            out[o + 2] = (face[i0 + 2] + face[i0 + 5] + face[i1 + 2] + face[i1 + 5]) * 0.25
            o += 3
    return out


def build_chain(size, faces):
    """入力キューブから 2×2 平均のミップ列(size → MIN_CHAIN_SIZE)を作る(設計書 §2.1)"""
    chain = [(size, faces)]
    while size > MIN_CHAIN_SIZE:
        faces = [downsample(f, size) for f in faces]
        size //= 2
        chain.append((size, faces))
    return chain


def chain_level(chain, size):
    for level_size, faces in chain:
        if level_size == size:
            return faces
    raise ValueError(f"ミップ列に {size}² の段がありません")


def sample_bilinear(faces, size, face, u, v):
    """1 面の中で双線形補間する(面の端はクランプ。隣の面へは跨がない)"""
    data = faces[face]
    fx = (u + 1.0) * 0.5 * size - 0.5
    fy = (v + 1.0) * 0.5 * size - 0.5
    last = size - 1
    if fx < 0.0: fx = 0.0
    elif fx > last: fx = float(last)
    if fy < 0.0: fy = 0.0
    elif fy > last: fy = float(last)
    x0 = int(fx)
    y0 = int(fy)
    ax = fx - x0
    ay = fy - y0
    x1 = x0 + 1 if x0 < last else x0
    y1 = y0 + 1 if y0 < last else y0
    i00 = (y0 * size + x0) * 3
    i10 = (y0 * size + x1) * 3
    i01 = (y1 * size + x0) * 3
    i11 = (y1 * size + x1) * 3
    w00 = (1.0 - ax) * (1.0 - ay)
    w10 = ax * (1.0 - ay)
    w01 = (1.0 - ax) * ay
    w11 = ax * ay
    return (data[i00]     * w00 + data[i10]     * w10 + data[i01]     * w01 + data[i11]     * w11,
            data[i00 + 1] * w00 + data[i10 + 1] * w10 + data[i01 + 1] * w01 + data[i11 + 1] * w11,
            data[i00 + 2] * w00 + data[i10 + 2] * w10 + data[i01 + 2] * w01 + data[i11 + 2] * w11)


# ----------------------------------------------------------------------------
#  DDS 読み込み(入力スカイキューブ: BGRA8 / RGBA8 の非圧縮キューブ)
# ----------------------------------------------------------------------------
DDS_HEADER_SIZE      = 128   # 'DDS ' + DDS_HEADER(124)
DX10_HEADER_SIZE     = 20
DDSCAPS2_CUBEMAP_ALL = 0x200 | 0xFC00
DDS_RESOURCE_MISC_TEXTURECUBE = 0x4


def parse_dds_header(blob):
    """DDS のヘッダを読み、辞書で返す(読み込み・検証の共通部)"""
    if blob[0:4] != b'DDS ':
        raise ValueError("DDS ではありません")
    height, width = struct.unpack_from('<II', blob, 12)
    mips = struct.unpack_from('<I', blob, 28)[0] or 1
    pf_flags, fourcc, bitcount, rmask, gmask, bmask, amask = struct.unpack_from('<I4sIIIII', blob, 80)
    caps, caps2 = struct.unpack_from('<II', blob, 108)
    info = {
        'width': width, 'height': height, 'mips': mips,
        'pf_flags': pf_flags, 'fourcc': fourcc, 'caps': caps, 'caps2': caps2,
        'dx10': False, 'format': None, 'misc': 0, 'array': 1,
        'is_cube': (caps2 & DDSCAPS2_CUBEMAP_ALL) == DDSCAPS2_CUBEMAP_ALL,
        'data_offset': DDS_HEADER_SIZE,
    }
    if (pf_flags & 0x4) and fourcc == b'DX10':
        fmt, dim, misc, arr, misc2 = struct.unpack_from('<IIIII', blob, DDS_HEADER_SIZE)
        info.update({'dx10': True, 'format': fmt, 'dimension': dim, 'misc': misc,
                     'array': arr, 'misc2': misc2,
                     'data_offset': DDS_HEADER_SIZE + DX10_HEADER_SIZE})
        info['is_cube'] = bool(misc & DDS_RESOURCE_MISC_TEXTURECUBE)
    elif (pf_flags & 0x40) and bitcount == 32:
        if rmask == 0x00FF0000 and gmask == 0x0000FF00 and bmask == 0x000000FF:
            info['format'] = DXGI_FORMAT_B8G8R8A8_UNORM
        elif rmask == 0x000000FF and gmask == 0x0000FF00 and bmask == 0x00FF0000:
            info['format'] = DXGI_FORMAT_R8G8B8A8_UNORM
    return info


def read_skycube(path):
    """スカイキューブ DDS を読み、(size, [面 0..5]) を返す。値は 0..1(変換しない)"""
    with open(path, 'rb') as f:
        blob = f.read()
    info = parse_dds_header(blob)
    fmt = info['format']
    if fmt in (DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB):
        order = (2, 1, 0)
    elif fmt in (DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):
        order = (0, 1, 2)
    else:
        raise ValueError(f"{path}: 対応していない形式です(BGRA8 / RGBA8 の非圧縮のみ)")
    if not info['is_cube']:
        raise ValueError(f"{path}: キューブマップではありません")
    size = info['width']
    if size != info['height'] or size & (size - 1) or size < SPECULAR_SIZE:
        raise ValueError(f"{path}: 1 面は {SPECULAR_SIZE} 以上の 2 の冪の正方形が必要です({size}x{info['height']})")

    # 面ごとに全ミップが並ぶ。使うのは各面の mip 0 だけ
    face_stride = 0
    s = size
    for _ in range(info['mips']):
        face_stride += s * s * 4
        s = max(1, s // 2)
    table = [i / 255.0 for i in range(256)]
    r_off, g_off, b_off = order
    faces = []
    for face in range(6):
        begin = info['data_offset'] + face * face_stride
        raw = blob[begin:begin + size * size * 4]
        out = array('f', bytes(size * size * 3 * 4))
        o = 0
        for i in range(0, len(raw), 4):
            out[o]     = table[raw[i + r_off]]
            out[o + 1] = table[raw[i + g_off]]
            out[o + 2] = table[raw[i + b_off]]
            o += 3
        faces.append(out)
    return size, faces


# ----------------------------------------------------------------------------
#  DDS 書き出し(DX10 ヘッダ / R16G16B16A16_FLOAT)
#
#  データ順は「面ごとに全ミップ」(DirectXTex の標準レイアウト)。
#  キューブは DX10 ヘッダの arraySize = 1(6 面で 1 キューブ)+ miscFlag TEXTURECUBE、
#  レガシー側の caps2 にもキューブ全面フラグを立てる(DirectXTex の書き出しと同じ)。
# ----------------------------------------------------------------------------
def pack_half_rgba(rgb, alpha, count):
    """RGB(array('f'))に alpha を足して half の bytes にする"""
    values = [0.0] * (count * 4)
    values[0::4] = rgb[0::3]
    values[1::4] = rgb[1::3]
    values[2::4] = rgb[2::3]
    if alpha != 0.0:
        values[3::4] = [alpha] * count
    # half の上限を超えると struct が例外を出すので丸めておく(LDR 入力では起きない)
    values = [65504.0 if x > 65504.0 else (-65504.0 if x < -65504.0 else x) for x in values]
    return struct.pack(f'<{count * 4}e', *values)


def dds_file_size(width, height, mips, faces):
    total = 0
    w, h = width, height
    for _ in range(mips):
        total += w * h * 8
        w, h = max(1, w // 2), max(1, h // 2)
    return DDS_HEADER_SIZE + DX10_HEADER_SIZE + total * faces


def write_dds_rgba16f(path, width, height, images, is_cube, alpha):
    """images[面][ミップ] = RGB の array('f')。2D は面 1 つ"""
    mips = len(images[0])
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH = 0x1, 0x2, 0x4, 0x8
    DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT = 0x1000, 0x20000
    DDSCAPS_COMPLEX, DDSCAPS_TEXTURE, DDSCAPS_MIPMAP = 0x8, 0x1000, 0x400000
    DDPF_FOURCC = 0x4

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT
    caps = DDSCAPS_TEXTURE
    if mips > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    if is_cube:
        caps |= DDSCAPS_COMPLEX

    header = bytearray(DDS_HEADER_SIZE + DX10_HEADER_SIZE)
    header[0:4] = b'DDS '
    struct.pack_into('<I', header, 4, 124)                  # dwSize
    struct.pack_into('<I', header, 8, flags)
    struct.pack_into('<I', header, 12, height)
    struct.pack_into('<I', header, 16, width)
    struct.pack_into('<I', header, 20, width * 8)           # dwPitchOrLinearSize(1 行のバイト数)
    struct.pack_into('<I', header, 28, mips)                # dwMipMapCount
    struct.pack_into('<I', header, 76, 32)                  # ddspf.dwSize
    struct.pack_into('<I', header, 80, DDPF_FOURCC)
    header[84:88] = b'DX10'
    struct.pack_into('<I', header, 108, caps)
    struct.pack_into('<I', header, 112, DDSCAPS2_CUBEMAP_ALL if is_cube else 0)
    # DDS_HEADER_DXT10
    struct.pack_into('<IIIII', header, DDS_HEADER_SIZE,
                     DXGI_FORMAT_R16G16B16A16_FLOAT,
                     3,                                     # D3D10_RESOURCE_DIMENSION_TEXTURE2D
                     DDS_RESOURCE_MISC_TEXTURECUBE if is_cube else 0,
                     1,                                     # arraySize(キューブは 6 面で 1)
                     0)                                     # miscFlags2(アルファモード未指定)

    with open(path, 'wb') as f:
        f.write(header)
        for face_mips in images:
            w, h = width, height
            for rgb in face_mips:
                f.write(pack_half_rgba(rgb, alpha, w * h))
                w, h = max(1, w // 2), max(1, h // 2)


def read_dds_rgba16f(path):
    """write_dds_rgba16f の書いた DDS を読み戻す(検証用)。images[面][ミップ] = RGBA の list"""
    with open(path, 'rb') as f:
        blob = f.read()
    info = parse_dds_header(blob)
    if not info['dx10'] or info['format'] != DXGI_FORMAT_R16G16B16A16_FLOAT:
        raise ValueError(f"{path}: DX10 / R16G16B16A16_FLOAT ではありません")
    face_count = 6 * info['array'] if info['is_cube'] else info['array']
    offset = info['data_offset']
    images = []
    for _ in range(face_count):
        mips = []
        w, h = info['width'], info['height']
        for _ in range(info['mips']):
            n = w * h * 4
            mips.append(list(struct.unpack_from(f'<{n}e', blob, offset)))
            offset += n * 2
            w, h = max(1, w // 2), max(1, h // 2)
        images.append(mips)
    info['file_size'] = len(blob)
    info['consumed'] = offset
    return info, images


# ----------------------------------------------------------------------------
#  irradiance(設計書 §2.2)
#
#  64² のミップを立体角で重み付けして SH 9 係数(L2)へ射影し、cos ローブで畳み込んで
#  (Ramamoorthi & Hanrahan 2001: A0 = π, A1 = 2π/3, A2 = π/4)、32² の各テクセル方向で評価して / π する。
# ----------------------------------------------------------------------------
def sh9_basis(x, y, z):
    return (0.282095,
            0.488603 * y,
            0.488603 * z,
            0.488603 * x,
            1.092548 * x * y,
            1.092548 * y * z,
            0.315392 * (3.0 * z * z - 1.0),
            1.092548 * x * z,
            0.546274 * (x * x - y * y))


SH9_BAND_SCALE = (math.pi,
                  2.0 * math.pi / 3.0, 2.0 * math.pi / 3.0, 2.0 * math.pi / 3.0,
                  math.pi / 4.0, math.pi / 4.0, math.pi / 4.0, math.pi / 4.0, math.pi / 4.0)


def project_sh9(faces, size):
    coeffs = [[0.0, 0.0, 0.0] for _ in range(9)]
    weight_sum = 0.0
    for face in range(6):
        data = faces[face]
        for y in range(size):
            for x in range(size):
                u, v = texel_uv(x, y, size)
                # テクセルの立体角(面上の微小面積 (2/size)² を単位球へ射影したもの)
                weight = 1.0 / (1.0 + u * u + v * v) ** 1.5
                dx, dy, dz = normalize(face_direction(face, u, v))
                i = (y * size + x) * 3
                r, g, b = data[i] * weight, data[i + 1] * weight, data[i + 2] * weight
                for k, basis in enumerate(sh9_basis(dx, dy, dz)):
                    c = coeffs[k]
                    c[0] += r * basis
                    c[1] += g * basis
                    c[2] += b * basis
                weight_sum += weight
    # 重みの総和を 4π に正規化する(近似の立体角でも一様な空で厳密に L が戻る)
    scale = 4.0 * math.pi / weight_sum
    return [[c * scale for c in coeff] for coeff in coeffs]


def generate_irradiance(chain):
    coeffs = project_sh9(chain_level(chain, SH_SOURCE_SIZE), SH_SOURCE_SIZE)
    convolved = [[c * SH9_BAND_SCALE[k] for c in coeffs[k]] for k in range(9)]
    size = IRRADIANCE_SIZE
    faces = []
    for face in range(6):
        out = make_face(size)
        o = 0
        for y in range(size):
            for x in range(size):
                basis = sh9_basis(*texel_direction(face, x, y, size))
                for ch in range(3):
                    e = 0.0
                    for k in range(9):
                        e += convolved[k][ch] * basis[k]
                    # SH の打ち切りで負になりうる(強いコントラストの空)ので 0 で止める
                    out[o + ch] = max(0.0, e / math.pi)
                o += 3
        faces.append(out)
    return faces


# ----------------------------------------------------------------------------
#  重点サンプリングの共通部
# ----------------------------------------------------------------------------
def radical_inverse(i):
    i = ((i << 16) | (i >> 16)) & 0xFFFFFFFF
    i = ((i & 0x55555555) << 1) | ((i & 0xAAAAAAAA) >> 1)
    i = ((i & 0x33333333) << 2) | ((i & 0xCCCCCCCC) >> 2)
    i = ((i & 0x0F0F0F0F) << 4) | ((i & 0xF0F0F0F0) >> 4)
    i = ((i & 0x00FF00FF) << 8) | ((i & 0xFF00FF00) >> 8)
    return (i & 0xFFFFFFFF) * 2.3283064365386963e-10


def importance_sample_ggx(i, count, alpha):
    """Hammersley 列の i 番目から GGX 分布に従うハーフベクトル(接空間、N = +Z)"""
    e1 = i / count
    e2 = radical_inverse(i)
    a2 = alpha * alpha
    phi = 2.0 * math.pi * e1
    cos_theta = math.sqrt((1.0 - e2) / (1.0 + (a2 - 1.0) * e2))
    sin_theta = math.sqrt(max(0.0, 1.0 - cos_theta * cos_theta))
    return (sin_theta * math.cos(phi), sin_theta * math.sin(phi), cos_theta)


def roughness_to_alpha(roughness):
    return max(roughness * roughness, ALPHA_MIN)


# ----------------------------------------------------------------------------
#  鏡面の前置フィルタ(設計書 §2.3)
#
#  Karis の N = V = R 近似。L = reflect(-V, H) を NdotL で重み付けして平均する。
#  サンプルごとに PDF からミップを選ぶ(filtered importance sampling。Křivánek & Colbert 2008):
#      Ωs = 1 / (N × pdf)、Ωp = 4π / (6 × 入力解像度²)、lod = 0.5 × log2(Ωs / Ωp) + 1
#  pdf = D × NdotH / (4 × VdotH) で、N = V なので D / 4。
#  接空間のサンプル列は roughness ごとに 1 回だけ作り、テクセルごとに基底を回すだけにする。
# ----------------------------------------------------------------------------
def build_specular_samples(roughness, sample_count, base_size, max_lod):
    alpha = roughness_to_alpha(roughness)
    a2 = alpha * alpha
    texel_solid_angle = 4.0 * math.pi / (6.0 * base_size * base_size)
    samples = []
    for i in range(sample_count):
        hx, hy, hz = importance_sample_ggx(i, sample_count, alpha)
        # V = N = (0, 0, 1) なので VdotH = NdotH = hz
        lx, ly, lz = 2.0 * hz * hx, 2.0 * hz * hy, 2.0 * hz * hz - 1.0
        if lz <= 0.0:
            continue
        d = (hz * a2 - hz) * hz + 1.0
        ggx = a2 / (math.pi * d * d)
        pdf = ggx * 0.25
        sample_solid_angle = 1.0 / (sample_count * pdf + 1e-6)
        lod = 0.5 * math.log2(sample_solid_angle / texel_solid_angle) + 1.0
        lod = min(max(lod, 0.0), float(max_lod))
        level = int(lod)
        if level >= max_lod:
            level, frac = max_lod, 0.0
        else:
            frac = lod - level
        samples.append((lx, ly, lz, lz, level, frac))
    return samples


_WORKER = {}


def _init_worker(chain, sample_tables):
    _WORKER['chain'] = chain
    _WORKER['samples'] = sample_tables


def _specular_row(task):
    """鏡面キューブ mip k の 1 行(ワーカーで実行)"""
    mip, face, y = task
    chain = _WORKER['chain']
    samples = _WORKER['samples'][mip]
    size = SPECULAR_SIZE >> mip
    row = []
    for x in range(size):
        nx, ny, nz = texel_direction(face, x, y, size)
        # N を Z 軸とする接空間の基底(T の回転方向は任意。GGX のローブは N 回りに対称)
        if abs(nz) < 0.999:
            tx, ty, tz = normalize((ny, -nx, 0.0))      # N と Z 軸の両方に直交
        else:
            tx, ty, tz = normalize((0.0, nz, -ny))      # N と X 軸の両方に直交
        bx, by, bz = ny * tz - nz * ty, nz * tx - nx * tz, nx * ty - ny * tx
        acc_r = acc_g = acc_b = 0.0
        weight_sum = 0.0
        for lx, ly, lz, weight, level, frac in samples:
            dx = tx * lx + bx * ly + nx * lz
            dy = ty * lx + by * ly + ny * lz
            dz = tz * lx + bz * ly + nz * lz
            f, u, v = direction_to_face_uv(dx, dy, dz)
            level_size, level_faces = chain[level]
            r, g, b = sample_bilinear(level_faces, level_size, f, u, v)
            if frac > 0.0:
                level_size, level_faces = chain[level + 1]
                r2, g2, b2 = sample_bilinear(level_faces, level_size, f, u, v)
                r += (r2 - r) * frac
                g += (g2 - g) * frac
                b += (b2 - b) * frac
            acc_r += r * weight
            acc_g += g * weight
            acc_b += b * weight
            weight_sum += weight
        inv = 1.0 / weight_sum
        row.extend((acc_r * inv, acc_g * inv, acc_b * inv))
    return mip, face, y, row


def generate_specular(chain, jobs, mip0_only=False):
    """images[面][ミップ] を返す。mip0_only は自己テスト用(mip 0 だけ作る)"""
    mip0 = chain_level(chain, SPECULAR_SIZE)
    images = [[array('f', mip0[face])] for face in range(6)]
    if mip0_only:
        return images

    base_size = chain[0][0]
    max_lod = len(chain) - 1
    sample_tables = {}
    for mip in range(1, SPECULAR_MIPS):
        roughness = mip / (SPECULAR_MIPS - 1)
        sample_tables[mip] = build_specular_samples(roughness, SPECULAR_SAMPLES, base_size, max_lod)
    for face in range(6):
        for mip in range(1, SPECULAR_MIPS):
            images[face].append(make_face(SPECULAR_SIZE >> mip))

    tasks = [(mip, face, y)
             for mip in range(1, SPECULAR_MIPS)
             for face in range(6)
             for y in range(SPECULAR_SIZE >> mip)]
    with make_pool(jobs, (chain, sample_tables)) as pool:
        for mip, face, y, row in pool.imap_unordered(_specular_row, tasks, chunksize=4):
            size = SPECULAR_SIZE >> mip
            begin = y * size * 3
            images[face][mip][begin:begin + size * 3] = array('f', row)
    return images


def make_pool(jobs, initargs):
    return multiprocessing.Pool(processes=jobs, initializer=_init_worker, initargs=initargs)


# ----------------------------------------------------------------------------
#  BRDF LUT(設計書 §2.4)
#
#  可視項は PBRFunctions.fx の V_SmithGGX(height-correlated Smith)と同じ式。
#      G_Vis = Vis × 4 × NdotL × VdotH / NdotH
#      A += (1 − Fc) × G_Vis、B += Fc × G_Vis(Fc = (1 − VdotH)^5)
# ----------------------------------------------------------------------------
def v_smith_ggx(n_dot_v, n_dot_l, alpha):
    a2 = alpha * alpha
    ggx_v = n_dot_l * math.sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2)
    ggx_l = n_dot_v * math.sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2)
    return 0.5 / max(ggx_v + ggx_l, 1e-5)


def integrate_brdf(n_dot_v, roughness, sample_count):
    alpha = roughness_to_alpha(roughness)
    vx, vz = math.sqrt(max(0.0, 1.0 - n_dot_v * n_dot_v)), n_dot_v   # V = (sinθ, 0, cosθ)
    a = b = 0.0
    for i in range(sample_count):
        hx, hy, hz = importance_sample_ggx(i, sample_count, alpha)
        v_dot_h = vx * hx + vz * hz
        lz = 2.0 * v_dot_h * hz - vz
        if lz <= 0.0:
            continue
        n_dot_l = min(lz, 1.0)
        n_dot_h = max(hz, 0.0)
        v_dot_h = min(max(v_dot_h, 0.0), 1.0)
        if n_dot_h <= 0.0:
            continue
        g_vis = v_smith_ggx(n_dot_v, n_dot_l, alpha) * 4.0 * n_dot_l * v_dot_h / n_dot_h
        m = 1.0 - v_dot_h
        fc = m * m * m * m * m
        a += (1.0 - fc) * g_vis
        b += fc * g_vis
    return a / sample_count, b / sample_count


def _lut_row(j):
    roughness = (j + 0.5) / LUT_SIZE
    row = []
    for i in range(LUT_SIZE):
        a, b = integrate_brdf((i + 0.5) / LUT_SIZE, roughness, LUT_SAMPLES)
        row.extend((a, b, 0.0))
    return j, row


def generate_lut(jobs):
    out = make_face(LUT_SIZE)
    with make_pool(jobs, (None, None)) as pool:
        for j, row in pool.imap_unordered(_lut_row, range(LUT_SIZE)):
            begin = j * LUT_SIZE * 3
            out[begin:begin + LUT_SIZE * 3] = array('f', row)
    return out


# ----------------------------------------------------------------------------
#  書き出しの入口
# ----------------------------------------------------------------------------
def output_paths(prefix):
    return prefix + "_Irradiance.dds", prefix + "_Specular.dds"


def generate_environment(size, faces, prefix, jobs, log=True):
    t0 = time.time()
    chain = build_chain(size, faces)
    t1 = time.time()
    irradiance = generate_irradiance(chain)
    t2 = time.time()
    specular = generate_specular(chain, jobs)
    t3 = time.time()
    irr_path, spec_path = output_paths(prefix)
    write_dds_rgba16f(irr_path, IRRADIANCE_SIZE, IRRADIANCE_SIZE, [[f] for f in irradiance], True, 1.0)
    write_dds_rgba16f(spec_path, SPECULAR_SIZE, SPECULAR_SIZE, specular, True, 1.0)
    if log:
        print(f"  ミップ列 {t1 - t0:.1f}s / irradiance {t2 - t1:.1f}s / 鏡面 {t3 - t2:.1f}s", file=sys.stderr)
        print(f"生成: {irr_path} ({os.path.getsize(irr_path)} bytes)")
        print(f"生成: {spec_path} ({os.path.getsize(spec_path)} bytes)")


def generate_lut_file(path, jobs, log=True):
    t0 = time.time()
    lut = generate_lut(jobs)
    write_dds_rgba16f(path, LUT_SIZE, LUT_SIZE, [[lut]], False, 0.0)
    if log:
        print(f"  LUT {time.time() - t0:.1f}s", file=sys.stderr)
        print(f"生成: {path} ({os.path.getsize(path)} bytes)")


# ----------------------------------------------------------------------------
#  検証の共通部
# ----------------------------------------------------------------------------
class Checker:
    def __init__(self):
        self.failed = 0

    def check(self, name, ok, detail):
        print(f"  [{'OK' if ok else 'NG'}] {name}: {detail}")
        if not ok:
            self.failed += 1


def check_header(checker, path, width, mips, is_cube):
    info, images = read_dds_rgba16f(path)
    expect_size = dds_file_size(width, width, mips, 6 if is_cube else 1)
    ok = (info['dx10'] and info['format'] == DXGI_FORMAT_R16G16B16A16_FLOAT
          and info['width'] == width and info['height'] == width
          and info['mips'] == mips and info['is_cube'] == is_cube and info['array'] == 1
          and ((info['caps2'] & DDSCAPS2_CUBEMAP_ALL) == DDSCAPS2_CUBEMAP_ALL) == is_cube
          and info['file_size'] == expect_size and info['consumed'] == expect_size)
    checker.check(f"DDS {os.path.basename(path)}", ok,
                  f"dx10={int(info['dx10'])} format={info['format']} {info['width']}x{info['height']} "
                  f"mips={info['mips']} cube(misc)={int(info['is_cube'])} caps2=0x{info['caps2']:X} "
                  f"array={info['array']} size={info['file_size']}(期待 {expect_size})")
    return images


def max_relative_error(images, expect):
    worst = 0.0
    for face_mips in images:
        for data in face_mips:
            for i in range(0, len(data), 4):
                for c in range(3):
                    worst = max(worst, abs(data[i + c] - expect[c]) / expect[c])
    return worst


def check_lut(checker, images):
    data = images[0][0]
    worst_low = 0.0      # 負になった量の最大
    worst_sum = 0.0      # A + B の最大
    for i in range(0, len(data), 4):
        worst_low = min(worst_low, data[i], data[i + 1])
        worst_sum = max(worst_sum, data[i] + data[i + 1])
    checker.check("LUT 範囲(A ≥ 0, B ≥ 0, A + B ≤ 1.0 + 0.01)",
                  worst_low >= -0.01 and worst_sum <= 1.01,
                  f"min(A,B)={worst_low:.5f} max(A+B)={worst_sum:.5f}")
    # 行 0(roughness 最小)の最右列
    i = (LUT_SIZE - 1) * 4
    total = data[i] + data[i + 1]
    checker.check("LUT 行 0 最右列の A + B ≥ 0.98", total >= 0.98,
                  f"A={data[i]:.5f} B={data[i + 1]:.5f} A+B={total:.5f}")


# ----------------------------------------------------------------------------
#  自己テスト(設計書 §2.5)
# ----------------------------------------------------------------------------
def make_test_cube(size, func):
    faces = []
    for face in range(6):
        out = make_face(size)
        o = 0
        for y in range(size):
            for x in range(size):
                out[o], out[o + 1], out[o + 2] = func(texel_direction(face, x, y, size))
                o += 3
        faces.append(out)
    return faces


def load_gen_skycube():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'SkyCubeGen', 'gen_skycube.py')
    if not os.path.exists(path):
        return None
    sys.dont_write_bytecode = True   # SkyCubeGen/ に __pycache__ を作らない
    spec = importlib.util.spec_from_file_location('gen_skycube', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def selftest(jobs):
    checker = Checker()
    t0 = time.time()

    # 面の向き: gen_skycube.py の face_direction と一致し、逆写像が往復する
    skycube = load_gen_skycube()
    points = [(-0.9, -0.7), (0.3, -0.8), (0.0, 0.0), (0.6, 0.25), (-0.4, 0.95)]
    mismatch = 0
    roundtrip = 0.0
    for face in range(6):
        for u, v in points:
            d = face_direction(face, u, v)
            if skycube is not None and d != skycube.face_direction(face, u, v):
                mismatch += 1
            f, u2, v2 = direction_to_face_uv(*d)
            roundtrip = max(roundtrip, abs(u2 - u) + abs(v2 - v) + (0.0 if f == face else 10.0))
    checker.check("面の向き(gen_skycube.py と一致・逆写像の往復)",
                  skycube is not None and mismatch == 0 and roundtrip < 1e-9,
                  f"gen_skycube={'読込' if skycube else '見つからない'} 不一致={mismatch} 往復誤差={roundtrip:.1e}")

    with tempfile.TemporaryDirectory() as tmp:
        # 一様な空 → irradiance / 鏡面の全テクセルが L
        # (入力は 128² で作る。鏡面 mip 0 = 入力そのもの。解像度は結果に影響しない)
        L = (0.2, 0.5, 0.8)
        uniform = make_test_cube(SPECULAR_SIZE, lambda d: L)
        prefix = os.path.join(tmp, 'Uniform')
        generate_environment(SPECULAR_SIZE, uniform, prefix, jobs, log=False)
        irr_path, spec_path = output_paths(prefix)
        irr = check_header(checker, irr_path, IRRADIANCE_SIZE, 1, True)
        spec = check_header(checker, spec_path, SPECULAR_SIZE, SPECULAR_MIPS, True)
        err = max_relative_error(irr, L)
        checker.check("irradiance 一様な空 → L(1% 以内)", err <= 0.01, f"最大相対誤差 {err * 100:.4f}%")
        err = max_relative_error(spec, L)
        checker.check("鏡面 一様な空 → 全ミップ L(1% 以内)", err <= 0.01, f"最大相対誤差 {err * 100:.4f}%")

        # 上半球 1・下半球 0 → mip 0 の +Y 面中央が 1、-Y 面中央が 0
        hemi = make_test_cube(SPECULAR_SIZE, lambda d: (1.0, 1.0, 1.0) if d[1] > 0.0 else (0.0, 0.0, 0.0))
        images = generate_specular(build_chain(SPECULAR_SIZE, hemi), jobs, mip0_only=True)
        hemi_path = os.path.join(tmp, 'Hemi.dds')
        write_dds_rgba16f(hemi_path, SPECULAR_SIZE, SPECULAR_SIZE, images, True, 1.0)
        _, hemi_read = read_dds_rgba16f(hemi_path)
        c = SPECULAR_SIZE // 2
        worst = 0.0
        detail = []
        for face, expect in ((2, 1.0), (3, 0.0)):
            data = hemi_read[face][0]
            values = []
            for y in (c - 1, c):
                for x in (c - 1, c):
                    i = (y * SPECULAR_SIZE + x) * 4
                    values.extend(data[i:i + 3])
            err = max(abs(v - expect) for v in values)
            worst = max(worst, err)
            detail.append(f"{'+Y' if face == 2 else '-Y'} 中央={sum(values) / len(values):.4f}")
        checker.check("鏡面の向き(上半球 1 / 下半球 0、0.01 以内)", worst <= 0.01, " ".join(detail))

        # LUT
        lut_path = os.path.join(tmp, 'BRDF_LUT.dds')
        generate_lut_file(lut_path, jobs, log=False)
        lut = check_header(checker, lut_path, LUT_SIZE, 1, False)
        check_lut(checker, lut)

    print(f"自己テスト {time.time() - t0:.1f}s: {'全項目 OK' if checker.failed == 0 else f'{checker.failed} 項目 NG'}")
    return 0 if checker.failed == 0 else 1


# ----------------------------------------------------------------------------
#  実データの検証(設計書 §4 P1 の自動評価)
# ----------------------------------------------------------------------------
def box_average(face_data, size, cx, cy, block):
    """入力面の、(cx, cy) を含む block × block の整列ブロックの平均"""
    x0 = (cx // block) * block
    y0 = (cy // block) * block
    acc = [0.0, 0.0, 0.0]
    for y in range(y0, y0 + block):
        for x in range(x0, x0 + block):
            i = (y * size + x) * 3
            for c in range(3):
                acc[c] += face_data[i + c]
    n = block * block
    return [a / n for a in acc]


def face_variance(data, size):
    """1 面の RGB 各チャンネルの分散の合計"""
    n = size * size
    total = 0.0
    for c in range(3):
        values = data[c::4][:n]
        mean = sum(values) / n
        total += sum((x - mean) * (x - mean) for x in values) / n
    return total


def verify(sky_path, prefix, lut_path, png_dir):
    checker = Checker()
    size, faces = read_skycube(sky_path)
    irr_path, spec_path = output_paths(prefix)
    irr = check_header(checker, irr_path, IRRADIANCE_SIZE, 1, True)
    spec = check_header(checker, spec_path, SPECULAR_SIZE, SPECULAR_MIPS, True)

    # 鏡面 mip 0 と入力の空を、6 面それぞれの中央と 4 隅付近の方向で比べる。
    # 方向は出力テクセルの中心から作り、入力側は方向 → 面 / uv の逆写像で独立に引く。
    # 入力側は出力 1 テクセルに対応する (入力解像度 / 128)² のブロック平均。
    block = size // SPECULAR_SIZE
    probes = [(63, 63), (64, 64), (2, 2), (125, 2), (2, 125), (125, 125)]
    worst = 0.0
    worst_at = None
    for face in range(6):
        out = spec[face][0]
        for x, y in probes:
            d = texel_direction(face, x, y, SPECULAR_SIZE)
            f, u, v = direction_to_face_uv(*d)
            ix = min(size - 1, int((u + 1.0) * 0.5 * size))
            iy = min(size - 1, int((v + 1.0) * 0.5 * size))
            expect = box_average(faces[f], size, ix, iy, block)
            i = (y * SPECULAR_SIZE + x) * 4
            for c in range(3):
                diff = abs(out[i + c] - expect[c]) * 255.0
                if diff > worst:
                    worst, worst_at = diff, (face, x, y, c)
    checker.check("鏡面 mip 0 と入力の空(6 面 × 中央 2 + 隅 4、2/255 以内)", worst <= 2.0,
                  f"最大差 {worst:.3f}/255(面 {worst_at[0]} 画素 {worst_at[1:3]} ch {worst_at[3]})"
                  if worst_at else "差 0")

    # +Y 面の画素分散が mip 1 → 5 で単調に減る
    variances = []
    for mip in range(SPECULAR_MIPS):
        variances.append(face_variance(spec[2][mip], SPECULAR_SIZE >> mip))
    rough = variances[1:]
    monotonic = all(rough[i] > rough[i + 1] for i in range(len(rough) - 1))
    checker.check("+Y 面の分散が mip 1 → 5 で単調減少", monotonic,
                  " ".join(f"mip{m}={variances[m]:.3e}" for m in range(SPECULAR_MIPS)))

    lut = None
    if lut_path:
        lut = check_header(checker, lut_path, LUT_SIZE, 1, False)
        check_lut(checker, lut)

    if png_dir:
        write_preview_pngs(png_dir, size, faces, irr, spec, lut)

    print(f"検証: {'全項目 OK' if checker.failed == 0 else f'{checker.failed} 項目 NG'}")
    return 0 if checker.failed == 0 else 1


# ----------------------------------------------------------------------------
#  確認用 PNG(6 面の十字展開図。クランプのみ・ガンマ無し)
#
#          [+Y]
#    [-X]  [+Z]  [+X]  [-Z]
#          [-Y]
#  face_direction の規約で継ぎ目が連続する並び。小さいミップは最近傍で 128² に拡大する。
# ----------------------------------------------------------------------------
CROSS_LAYOUT = {2: (1, 0), 1: (0, 1), 4: (1, 1), 0: (2, 1), 5: (3, 1), 3: (1, 2)}


def write_png(path, width, height, rgb_rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + bytes(row) for row in rgb_rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def to_byte(x):
    return 0 if x <= 0.0 else (255 if x >= 1.0 else int(x * 255.0 + 0.5))


def write_cross_png(path, face_images, size, channels, view=128):
    """face_images[面] = size² のフラット配列(channels 個ずつ)"""
    width, height = view * 4, view * 3
    rows = [bytearray(width * 3) for _ in range(height)]   # 余白は黒
    for face, (cx, cy) in CROSS_LAYOUT.items():
        data = face_images[face]
        for py in range(view):
            sy = py * size // view
            row = rows[cy * view + py]
            for px in range(view):
                sx = px * size // view
                i = (sy * size + sx) * channels
                o = (cx * view + px) * 3
                row[o]     = to_byte(data[i])
                row[o + 1] = to_byte(data[i + 1])
                row[o + 2] = to_byte(data[i + 2])
    write_png(path, width, height, rows)
    return path


def write_preview_pngs(png_dir, size, faces, irr, spec, lut):
    os.makedirs(png_dir, exist_ok=True)
    written = []
    chain = build_chain(size, faces)
    written.append(write_cross_png(os.path.join(png_dir, 'input_128.png'),
                                   chain_level(chain, SPECULAR_SIZE), SPECULAR_SIZE, 3))
    for mip in range(SPECULAR_MIPS):
        written.append(write_cross_png(os.path.join(png_dir, f'specular_mip{mip}.png'),
                                       [spec[f][mip] for f in range(6)], SPECULAR_SIZE >> mip, 4))
    written.append(write_cross_png(os.path.join(png_dir, 'irradiance.png'),
                                   [irr[f][0] for f in range(6)], IRRADIANCE_SIZE, 4))
    if lut is not None:
        data = lut[0][0]
        rows = []
        for j in range(LUT_SIZE):
            row = bytearray(LUT_SIZE * 3)
            for i in range(LUT_SIZE):
                k = (j * LUT_SIZE + i) * 4
                row[i * 3], row[i * 3 + 1], row[i * 3 + 2] = to_byte(data[k]), to_byte(data[k + 1]), 0
            rows.append(row)
        path = os.path.join(png_dir, 'brdf_lut.png')
        write_png(path, LUT_SIZE, LUT_SIZE, rows)
        written.append(path)
    for path in written:
        print(f"PNG: {os.path.abspath(path)}")


# ----------------------------------------------------------------------------
#  コマンドライン
# ----------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="IBL 用 DDS(irradiance / 鏡面 / BRDF LUT)を生成する")
    parser.add_argument('paths', nargs='*', help="<入力スカイキューブ.dds> <出力の接頭辞>")
    parser.add_argument('--lut', metavar='PATH', help="BRDF LUT の出力先(--verify 時は検証対象)")
    parser.add_argument('--selftest', action='store_true', help="設計書 §2.5 の自己テスト")
    parser.add_argument('--verify', action='store_true', help="生成物を実データで検証する(設計書 §4 P1)")
    parser.add_argument('--png', metavar='DIR', help="確認用 PNG の出力先(生成・検証のどちらでも)")
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1, help="並列数")
    args = parser.parse_args()

    if args.selftest:
        return selftest(args.jobs)
    if args.paths and len(args.paths) != 2:
        parser.error("位置引数は <入力スカイキューブ.dds> <出力の接頭辞> の 2 つ")
    if args.verify:
        if not args.paths:
            parser.error("--verify には <入力スカイキューブ.dds> <出力の接頭辞> が要る")
        return verify(args.paths[0], args.paths[1], args.lut, args.png)
    if not args.paths and not args.lut:
        parser.print_help()
        return 2

    t0 = time.time()
    if args.paths:
        size, faces = read_skycube(args.paths[0])
        generate_environment(size, faces, args.paths[1], args.jobs)
    if args.lut:
        generate_lut_file(args.lut, args.jobs)
    print(f"所要 {time.time() - t0:.1f}s")
    if args.png and args.paths:
        return verify(args.paths[0], args.paths[1], args.lut, args.png)
    return 0


if __name__ == '__main__':
    sys.exit(main())
