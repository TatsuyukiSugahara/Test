# -*- coding: utf-8 -*-
"""スピード感のあるループ BGM の生成ツール。

152BPM / A マイナーのシンセループ (4つ打ちキック + オフビートハット +
8分ベース + 16分アルペジオ + 薄いパッド) を 16 小節 (約25秒) 合成し、
Assets/Sound/AquaDashBGM.wav (44.1kHz / 16bit / stereo) へ出力する。
エコーはバッファ長の剰余で書き込むため、末尾→先頭のループが途切れない。
依存ライブラリなし (標準ライブラリのみ)。

使い方:
  python generate_bgm.py            (出力: ../Assets/Sound/AquaDashBGM.wav)
"""

import math
import os
import random
import struct
import sys
import wave

SR      = 44100
BPM     = 152.0
BARS    = 16
SPB     = 60.0 / BPM                     # 1 拍 [s]
TOTAL_S = BARS * 4 * SPB
FRAMES  = int(round(TOTAL_S * SR))       # ループ全長 [frame]

MASTER  = 0.85                           # 正規化後のピーク

# コード進行 (4 小節ごと): Am → F → C → G。音名は A4=440 の半音番号 (A4=0)。
CHORDS = [
    [-12, -9, -5],    # Am (A3, C4, E4)
    [-16, -12, -9],   # F  (F3, A3, C4)
    [-21, -17, -14],  # C  (C3, E3, G3)
    [-14, -10, -7],   # G  (G3, B3, D4)
]
BARS_PER_CHORD = 4


def freq(semi):
    return 440.0 * (2.0 ** (semi / 12.0))


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "Sound", "AquaDashBGM.wav"))

    left  = [0.0] * FRAMES
    right = [0.0] * FRAMES
    rng   = random.Random(20260908)

    def add(t, dur, gen, gain, pan=0.0):
        """t 秒から dur 秒、gen(ローカル秒)→サンプルを合成する (ループ剰余書き込み)。"""
        start = int(t * SR)
        n = int(dur * SR)
        gl = gain * min(1.0, 1.0 - pan)
        gr = gain * min(1.0, 1.0 + pan)
        for i in range(n):
            v = gen(i / SR)
            j = (start + i) % FRAMES
            left[j]  += v * gl
            right[j] += v * gr

    def chord_at(bar):
        return CHORDS[(bar // BARS_PER_CHORD) % len(CHORDS)]

    # --- ドラム ---
    for bar in range(BARS):
        for beat in range(4):
            t = (bar * 4 + beat) * SPB

            # キック (4つ打ち): ピッチが 150→50Hz へ落ちるサイン + クリック。
            def kick(lt):
                pitch = 50.0 + 100.0 * math.exp(-lt * 30.0)
                body  = math.sin(2.0 * math.pi * pitch * lt) * math.exp(-lt * 12.0)
                click = math.exp(-lt * 400.0) * 0.5
                return body + click
            add(t, 0.30, kick, 0.85)

            # スネア (2, 4 拍): ノイズ + 180Hz ボディ。
            if beat in (1, 3):
                def snare(lt):
                    return (rng.uniform(-1, 1) * 0.8
                            + math.sin(2.0 * math.pi * 180.0 * lt) * 0.4) * math.exp(-lt * 22.0)
                add(t, 0.18, snare, 0.45)

            # ハット (オフビート 8分): 差分ノイズで高域寄りに。
            def hat(lt, _p=[0.0]):
                n = rng.uniform(-1, 1)
                v = n - _p[0]
                _p[0] = n
                return v * math.exp(-lt * 60.0)
            add(t + SPB * 0.5, 0.06, hat, 0.30, pan=-0.35)

    # --- ベース (8分でルートをオクターブバウンス) ---
    for bar in range(BARS):
        root = chord_at(bar)[0] - 12   # 1 オクターブ下
        for eighth in range(8):
            t = bar * 4 * SPB + eighth * SPB * 0.5
            semi = root + (12 if eighth % 4 == 2 else 0)
            f = freq(semi)
            def bass(lt, f=f):
                # 奇数倍音でノコギリ寄りの太い音。短いディケイで刻む。
                v = (math.sin(2.0 * math.pi * f * lt)
                     + 0.5 * math.sin(2.0 * math.pi * f * 2.0 * lt)
                     + 0.25 * math.sin(2.0 * math.pi * f * 3.0 * lt))
                env = min(1.0, lt / 0.005) * math.exp(-lt * 7.0)
                return v * env
            add(t, SPB * 0.5 * 0.95, bass, 0.40)

    # --- アルペジオ (16分。コードトーンを 2 オクターブで上昇) ---
    for bar in range(BARS):
        tones = chord_at(bar)
        seq = [tones[0], tones[1], tones[2], tones[0] + 12,
               tones[1] + 12, tones[2] + 12, tones[0] + 24, tones[2] + 12]
        for six in range(16):
            t = bar * 4 * SPB + six * SPB * 0.25
            f = freq(seq[six % len(seq)] + 12)
            def arp(lt, f=f):
                # 奇数倍音の矩形波寄り。
                v = (math.sin(2.0 * math.pi * f * lt)
                     + math.sin(2.0 * math.pi * f * 3.0 * lt) / 3.0
                     + math.sin(2.0 * math.pi * f * 5.0 * lt) / 5.0)
                env = min(1.0, lt / 0.003) * math.exp(-lt * 18.0)
                return v * env
            add(t, SPB * 0.25 * 0.9, arp, 0.26, pan=0.30)

    # --- パッド (コードを全音符で薄く。左右で僅かにデチューン) ---
    for bar in range(0, BARS, BARS_PER_CHORD):
        tones = chord_at(bar)
        t = bar * 4 * SPB
        dur = BARS_PER_CHORD * 4 * SPB
        for semi in tones:
            f = freq(semi + 12)
            def padL(lt, f=f * 0.997):
                env = min(1.0, lt / 0.4) * min(1.0, (dur - lt) / 0.4 if lt < dur else 0.0)
                return math.sin(2.0 * math.pi * f * lt) * env
            def padR(lt, f=f * 1.003):
                env = min(1.0, lt / 0.4) * min(1.0, (dur - lt) / 0.4 if lt < dur else 0.0)
                return math.sin(2.0 * math.pi * f * lt) * env
            add(t, dur, padL, 0.10, pan=-1.0)
            add(t, dur, padR, 0.10, pan=1.0)

    # --- 正規化して書き出し ---
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right))
    scale = MASTER / peak if peak > 0.0 else 0.0
    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for i in range(FRAMES):
            frames += struct.pack("<hh",
                                  int(max(-1.0, min(1.0, left[i] * scale)) * 32767),
                                  int(max(-1.0, min(1.0, right[i] * scale)) * 32767))
        w.writeframes(bytes(frames))

    print(f"wrote {out_path} ({TOTAL_S:.2f}s loop, {BPM:.0f}BPM, {SR}Hz stereo)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
