# -*- coding: utf-8 -*-
"""コイン(リング)取得 SE の生成ツール。

専用音源アセットが無いため、リング取得音風の 2 音チャイム
(B5 987.77Hz → E6 1318.51Hz、倍音 + 指数減衰)を合成して
Assets/Sound/CoinGet.wav (44.1kHz / 16bit / mono) へ出力する。
依存ライブラリなし (標準ライブラリのみ)。

使い方:
  python generate_coin_se.py            (出力: ../Assets/Sound/CoinGet.wav)
  python generate_coin_se.py <out.wav>
"""

import math
import os
import struct
import sys
import wave

SAMPLE_RATE = 44100
DURATION    = 0.38          # 全体の長さ [s]
AMPLITUDE   = 0.62          # ピーク振幅 (クリップ余裕を残す)

# (周波数 [Hz], 開始 [s], 減衰タイム [s], 相対音量)
NOTES = [
    (987.77,  0.000, 0.060, 0.85),   # B5 (短い前打音)
    (1318.51, 0.070, 0.240, 1.00),   # E6 (主音、長めに余韻)
]

# 倍音構成 (倍率, 相対振幅)。きらびやかさを足す。
HARMONICS = [(1.0, 1.00), (2.0, 0.38), (3.0, 0.14)]

ATTACK = 0.002              # クリックノイズ防止のアタック [s]


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "Sound", "CoinGet.wav"))

    frame_count = int(SAMPLE_RATE * DURATION)
    samples = [0.0] * frame_count

    for freq, start, decay, volume in NOTES:
        start_i = int(start * SAMPLE_RATE)
        for i in range(start_i, frame_count):
            t = (i - start_i) / SAMPLE_RATE
            env = math.exp(-t / decay)
            if t < ATTACK:
                env *= t / ATTACK
            v = 0.0
            for mul, amp in HARMONICS:
                v += amp * math.sin(2.0 * math.pi * freq * mul * t)
            samples[i] += volume * env * v

    peak = max(abs(s) for s in samples)
    scale = AMPLITUDE / peak if peak > 0.0 else 0.0

    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for s in samples:
            frames += struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767))
        w.writeframes(bytes(frames))

    print(f"wrote {out_path} ({DURATION}s, {SAMPLE_RATE}Hz, 16bit mono)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
