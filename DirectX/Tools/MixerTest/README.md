# MixerTest — `SoftwareMixer` 単体テストツール

Mac 移植(→ [設計書/Mac移植設計.md](../../設計書/Mac移植設計.md) §5 / §9 P1)で新設した
プラットフォーム非依存ソフトウェアミキサ `aq::sound::SoftwareMixer` を、
オーディオデバイス無しで検証するためのコマンドラインツール。

P1 の評価項目「**2 ボイス(片方ピッチ 1.5、出力行列で左右反転)のミックス結果が
期待波形になる**」を `--selftest` で自動判定する。

---

## 1. 使い方

### セルフテスト(P1 評価項目)

```
MixerTest --selftest [-o out.wav]
```

- 入力ファイル不要。ランプ(一次関数)波形を 2 ボイス分メモリ上で生成する。
  **線形リサンプルは 1 次補間なので、入力が一次関数なら任意のリサンプル比で
  出力が解析的に決まる**。これを期待波形として突き合わせる。
- 検証している内容:

  | 項目 | 内容 |
  |---|---|
  | ミックス | 2 ボイスの加算 |
  | 線形リサンプル | ボイス B にピッチ 1.5(`SetFrequencyRatio` 相当) |
  | 出力行列 | ボイス B に左右反転行列(`SetOutputMatrix` 相当) |
  | ボイス音量 | ボイス A = 0.5 / B = 1.0 |
  | バス音量 | SE = 1.0 / BGM = 0.75 |
  | マスタ音量 | 0.8 |
  | 消費フレーム数 | `GetConsumedFrames` がピッチに比例して増える |
  | 自然終了 | `endOfStream` 投入 → `IsFinished` が立つ → `ReleaseVoice` でスロットが戻る |

- 全フレームで許容誤差 `1.0e-5` 以内なら `[selftest] PASS` を出して終了コード 0。
  1 フレームでも外れたらフレーム番号と実測/期待値を出して終了コード 1。
- `-o` を付けると結果を WAV にも書く(耳で確認したいとき用)。

### 実 WAV のミックス

```
MixerTest -o out.wav --in bgm.wav --bus BGM --vol 0.6 \
                     --in se.wav  --pitch 1.5 --swap
```

| オプション | 意味 |
|---|---|
| `-o` / `--out <path>` | 出力 WAV(必須) |
| `--rate <hz>` | 出力サンプルレート(既定 48000) |
| `--channels <n>` | 出力チャンネル数(既定 2) |
| `--master <v>` | マスタ音量(既定 1.0) |
| `--seconds <s>` | 出力長の上限(既定 0 = 全ボイス終了まで) |
| `--pcm16` | 出力を 16bit PCM にする(既定 32bit IEEE float) |
| `--in <path>` | 入力 WAV を 1 ボイス追加する |
| `--pitch <r>` | 直前の `--in` のピッチ(既定 1.0) |
| `--vol <v>` | 直前の `--in` のボイス音量(既定 1.0) |
| `--swap` | 直前の `--in` に左右反転の出力行列を掛ける |
| `--bus <name>` | 直前の `--in` のバス(`Master` / `BGM` / `SE` / `Voice`。既定 `SE`) |

入力は `WavDecoder` が読める RIFF/WAVE(PCM 整数 / IEEE float / EXTENSIBLE)。
サンプルレートが出力と違う場合はミキサの線形リサンプルが吸収する。

---

## 2. ビルドに必要なもの

**`Tools/MixerTest/CMakeLists.txt` は本体側で用意する**(このディレクトリには置かない)。
必要な情報は以下のとおり。

### ソース

| ファイル | 備考 |
|---|---|
| `Tools/MixerTest/Main.cpp` | このツール本体。`aq.h`(PCH)には依存しない |
| `aqEngine/Sound/Mixer/SoftwareMixer.cpp` | 被テスト対象。先頭で `aq.h` を include する |
| `aqEngine/Sound/Decoder/WavDecoder.cpp` | WAV 読み込み。先頭で `aq.h` を include する |

`Main.cpp` が include するヘッダは `Sound/Mixer/SoftwareMixer.h` と
`Sound/Decoder/WavDecoder.h` の 2 本だけで、どちらも `SoundTypes.h` / `SoundFwd.h` と
標準ヘッダしか要求しない(Windows SDK も D3D も要らない)。

### 依存

- C++20、標準ライブラリのみ。外部ライブラリ・OS API へのリンクは無し。
- WAV 書き出しはこのツール内に実装してある(`WriteWav`)。

### 推奨: `aqEngine` にリンクする

`SoftwareMixer.cpp` / `WavDecoder.cpp` は先頭で `aq.h` を include するため、
これらを MixerTest のターゲットへ直接足すと `aqEngine` のインクルードパス一式が要る。
`aqEngine` は静的ライブラリなので、リンクすれば必要なオブジェクトだけが引かれる。

```cmake
add_executable(MixerTest "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")
target_link_libraries(MixerTest PRIVATE aqEngine)   # include パスも PUBLIC で付いてくる
```

### 代替: 3 本を直接コンパイルする

`aqEngine` を丸ごとビルドしたくない場合は、上記 3 本を足したうえで
`aqEngine/` とリポジトリルートをインクルードパスに入れる(`aq.h` の解決に必要)。

```cmake
add_executable(MixerTest
	"${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp"
	"${CMAKE_SOURCE_DIR}/aqEngine/Sound/Mixer/SoftwareMixer.cpp"
	"${CMAKE_SOURCE_DIR}/aqEngine/Sound/Decoder/WavDecoder.cpp")
target_include_directories(MixerTest PRIVATE
	"${CMAKE_SOURCE_DIR}/aqEngine" "${CMAKE_SOURCE_DIR}")
```

### MSBuild(`DirectX.sln`)側

CMake 専用のツールなので `.vcxproj` は用意していない。Visual Studio から動かしたい
場合は CMake 生成側(`build/<preset>/`)のソリューションを使う。

---

## 3. 現状の非カバー範囲

- **`SubmitClipRegion`(ゼロコピー再生)とループ区間**。`SoundClip` が
  `Resource/Resource.h` を要求し、ツールが `aq.h` 依存になるため今回は入れていない。
  ミキサ側の実装はあるので、P4 で `CoreAudioSoundVoice` を通した実機再生で確認する。
- **スレッド境界の実負荷テスト**。本ツールは投入と `Render` を同一スレッドから
  呼ぶ(SPSC リングの経路自体は同じものを通る)。実際の 2 スレッド動作は
  P4 の CoreAudio render callback で確認する。
