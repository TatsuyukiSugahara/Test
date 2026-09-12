# -*- coding: utf-8 -*-
"""エアトリック SE の生成ツール。

専用音源アセットが無いため、2 本の SE を合成して
Assets/Sound/ 以下へ出力する (44.1kHz / 16bit / mono)。
依存ライブラリなし (標準ライブラリのみ)。

  TrickSpin.wav : 回転開始。短く軽い「シュン」(上へ掃けるノイズ + 短い上昇トーン)
  TrickLand.wav : 着地成功。上昇する短いジングル (3 音のアルペジオ)

使い方:
  python generate_trick_se.py            (出力: ../Assets/Sound/TrickSpin.wav と TrickLand.wav)
  python generate_trick_se.py <out_dir>
"""

import math
import os
import random
import struct
import sys
import wave

SAMPLE_RATE = 44100

# --- TrickSpin (回転開始) --------------------------------------------------
SPIN_FILE_NAME  = "TrickSpin.wav"
SPIN_DURATION   = 0.18          # 全体の長さ [s] (連打されるので短く切る)
SPIN_AMPLITUDE  = 0.52          # ピーク振幅 (着地音より控えめにする)
SPIN_NOISE_VOL  = 1.00          # ノイズ成分 (スウッシュ本体) の音量
SPIN_LP_START   = 0.10          # ローパスの追従係数 (開始 = 低く篭った音)
SPIN_LP_END     = 0.70          # 同 (終端 = 明るく抜ける音)
SPIN_SEED       = 20260912      # 毎回同じ WAV になるよう固定する
SPIN_TONE_VOL   = 0.30          # 芯を足す上昇トーンの音量
SPIN_FREQ_START = 520.0         # [Hz] 開始ピッチ
SPIN_FREQ_END   = 1750.0        # [Hz] 終端ピッチ
SPIN_ATTACK     = 0.004         # 立ち上がり [s] (クリックノイズ防止)
SPIN_RELEASE    = 0.090         # 終端の減衰タイム [s]

# --- TrickLand (着地成功) --------------------------------------------------
LAND_FILE_NAME = "TrickLand.wav"
LAND_DURATION  = 0.46           # 全体の長さ [s]
LAND_AMPLITUDE = 0.70           # ピーク振幅 (クリップ余裕を残す)
# (周波数 [Hz], 開始 [s], 減衰タイム [s], 相対音量)。上昇するアルペジオにする。
LAND_NOTES = [
    (659.26,  0.000, 0.090, 0.80),   # E5
    (987.77,  0.075, 0.110, 0.90),   # B5
    (1318.51, 0.150, 0.280, 1.00),   # E6 (主音、長めに余韻)
]
# 倍音構成 (倍率, 相対振幅)。きらびやかさを足す。
LAND_HARMONICS = [(1.0, 1.00), (2.0, 0.34), (3.0, 0.12)]
LAND_ATTACK    = 0.002          # クリックノイズ防止のアタック [s]


def write_wav(out_path, samples, amplitude):
    """正規化して 16bit mono WAV として書き出す。"""
    peak  = max(abs(s) for s in samples)
    scale = amplitude / peak if peak > 0.0 else 0.0
    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for s in samples:
            frames += struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767))
        w.writeframes(bytes(frames))
    print(f"wrote {out_path} ({len(samples) / SAMPLE_RATE:.3f}s, {SAMPLE_RATE}Hz, 16bit mono)")


def render_spin():
    """回転開始の「シュン」。帯域を上へ掃くノイズに短い上昇トーンを重ねる。"""
    frame_count = int(SAMPLE_RATE * SPIN_DURATION)
    samples = [0.0] * frame_count

    rng        = random.Random(SPIN_SEED)
    lp_value   = 0.0
    tone_phase = 0.0

    for i in range(frame_count):
        t     = i / SAMPLE_RATE
        ratio = i / frame_count          # 0..1 の進行度

        # 包絡: 短いアタック → 終盤で指数減衰。
        env = 1.0
        if t < SPIN_ATTACK:
            env = t / SPIN_ATTACK
        tail = SPIN_DURATION - t
        if tail < SPIN_RELEASE:
            env *= math.exp(-(SPIN_RELEASE - tail) / (SPIN_RELEASE * 0.40))

        # ノイズ: 1 次ローパスの係数を上げていき、帯域が上へ掃けるようにする。
        lp_coef   = SPIN_LP_START + (SPIN_LP_END - SPIN_LP_START) * ratio
        lp_value += lp_coef * (rng.uniform(-1.0, 1.0) - lp_value)
        v = SPIN_NOISE_VOL * lp_value

        # トーン: 周波数を線形に上げる (位相を積分して連続にする)。
        freq = SPIN_FREQ_START + (SPIN_FREQ_END - SPIN_FREQ_START) * ratio
        tone_phase += 2.0 * math.pi * freq / SAMPLE_RATE
        v += SPIN_TONE_VOL * math.sin(tone_phase)

        samples[i] = env * v

    return samples


def render_land():
    """着地成功のジングル。上昇する 3 音のアルペジオ。"""
    frame_count = int(SAMPLE_RATE * LAND_DURATION)
    samples = [0.0] * frame_count

    for freq, start, decay, volume in LAND_NOTES:
        start_i = int(start * SAMPLE_RATE)
        for i in range(start_i, frame_count):
            t   = (i - start_i) / SAMPLE_RATE
            env = math.exp(-t / decay)
            if t < LAND_ATTACK:
                env *= t / LAND_ATTACK
            v = 0.0
            for mul, amp in LAND_HARMONICS:
                v += amp * math.sin(2.0 * math.pi * freq * mul * t)
            samples[i] += volume * env * v

    return samples


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "Sound"))

    write_wav(os.path.join(out_dir, SPIN_FILE_NAME), render_spin(), SPIN_AMPLITUDE)
    write_wav(os.path.join(out_dir, LAND_FILE_NAME), render_land(), LAND_AMPLITUDE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
