# -*- coding: utf-8 -*-
"""ブーストパッド SE の生成ツール。

専用音源アセットが無いため、「上昇するスウッシュ」(帯域を上へ掃くノイズ +
上昇するピッチのトーン)を合成して
Assets/Sound/Boost.wav (44.1kHz / 16bit / mono) へ出力する。
依存ライブラリなし (標準ライブラリのみ)。

使い方:
  python generate_boost_se.py            (出力: ../Assets/Sound/Boost.wav)
  python generate_boost_se.py <out.wav>
"""

import math
import os
import random
import struct
import sys
import wave

SAMPLE_RATE = 44100
DURATION    = 0.42          # 全体の長さ [s]
AMPLITUDE   = 0.72          # ピーク振幅 (クリップ余裕を残す)

# ノイズ成分 (スウッシュ本体)
NOISE_VOLUME     = 1.00
NOISE_LP_START   = 0.06     # ローパスの追従係数 (開始 = 低く篭った音)
NOISE_LP_END     = 0.55     # 同 (終端 = 明るく抜ける音)
NOISE_SEED       = 20260912 # 毎回同じ WAV になるよう固定する

# トーン成分 (上昇するピッチ)
TONE_VOLUME      = 0.45
TONE_FREQ_START  = 220.0    # [Hz] 開始ピッチ
TONE_FREQ_END    = 1400.0   # [Hz] 終端ピッチ
TONE_HARMONICS   = [(1.0, 1.00), (2.0, 0.30)]   # (倍率, 相対振幅)

ATTACK  = 0.010             # 立ち上がり [s] (クリックノイズ防止)
RELEASE = 0.160             # 終端の減衰タイム [s]


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "Sound", "Boost.wav"))

    frame_count = int(SAMPLE_RATE * DURATION)
    samples = [0.0] * frame_count

    rng = random.Random(NOISE_SEED)
    lp_value  = 0.0
    tone_phase = 0.0

    for i in range(frame_count):
        t     = i / SAMPLE_RATE
        ratio = i / frame_count          # 0..1 の進行度

        # 包絡: 短いアタック → 終盤で指数減衰。
        env = 1.0
        if t < ATTACK:
            env = t / ATTACK
        tail = DURATION - t
        if tail < RELEASE:
            env *= math.exp(-(RELEASE - tail) / (RELEASE * 0.45))

        # ノイズ: 1 次ローパスの係数を上げていき、帯域が上へ掃けるようにする。
        lp_coef  = NOISE_LP_START + (NOISE_LP_END - NOISE_LP_START) * ratio
        lp_value += lp_coef * (rng.uniform(-1.0, 1.0) - lp_value)
        v = NOISE_VOLUME * lp_value

        # トーン: 周波数を線形に上げる (位相を積分して連続にする)。
        freq = TONE_FREQ_START + (TONE_FREQ_END - TONE_FREQ_START) * ratio
        tone_phase += 2.0 * math.pi * freq / SAMPLE_RATE
        for mul, amp in TONE_HARMONICS:
            v += TONE_VOLUME * amp * math.sin(tone_phase * mul)

        samples[i] = env * v

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
