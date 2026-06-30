# サウンドシステム 残作業（続き）

最終更新: 2026-06-30 / ブランチ `feature/sound-system`（main へマージ）。サウンド系は一旦終了。

低レベル `SoundEngine`（XAudio2）＋ データ駆動オーサリング層（`Sound/Authoring/`）は実装済み。
詳細設計は [Sound設計.md](Sound設計.md) / [AudioAuthoring設計.md](AudioAuthoring設計.md)、現状サマリは Sound設計.md §14。

## 実装済み
- SoundEngine: 2D ワンショット / ストリーミング(BGM・背圧・ループ) / 3D(`Mixer3D`: 距離減衰・パン・ドップラー) / バス(Master/BGM/SE/Voice) / 音量・ピッチ・フェード / 全体&バス単位 Pause・Stop
- リソース: `SoundClip` + 拡張子ディスパッチ。WAV / WAVストリーム / Media Foundation(MP3/AAC/WMA/M4A)
- ECS: `AudioListenerComponent` / `AudioSourceComponent` / `SoundSystem`
- オーサリング層: Event / Kind / Sound / Random / Sequence / Switch / 3D(`AudioEventEmitterComponent`) / RTPC / State+stateRules / 自動ダッキング、ImGui パネル `AudioAuthoringPanel`

## 続き（未実装・優先度順の目安）
1. ~~**音量の調停**~~ ✅ 完了。
   - バス: SoundEngine に `busDuck_`（base と独立）+ `SetBusDuck` / `ApplyBus`（effective = base × duck）。ダッキングは `SetBusVolume` でなく `SetBusDuck` を使う。
   - インスタンス: AudioDirector を唯一の音量所有者にし、`base × RTPC × fadeGate` を `ApplyInstanceVolumes(dt)` で毎フレーム合成。フェードは SoundEngine 側でなく AudioDirector の `fadeGate`(VolumeEnvelope) で所有（二重所有を解消）。
2. ~~**Blend コンテナ**~~ ✅ 完了。`ObjectType::Blend` + `BlendLayer{child,rtpc,curve}`。`PlayTopObject` が各レイヤを同時再生（`PlayResolved`）。レイヤ別 RTPC 音量を `ApplyInstanceVolumes` の合成に追加（base × RTPC × layerRTPC × fadeGate）。※ ネスト Blend（Switch/Random 配下）は将来。
3. ~~**専用オーサリングエディタ（第一段）**~~ ✅ 完了。`AudioAuthoringPanel` を拡張: RTPC ライブスライダ + カーブ表示(`PlotLines`)、State/Switch の値ボタン（現在値ハイライト・自動列挙）、Objects ブラウザ（任意再生 `DebugPlayObject`）。
   - 続き候補: ノードグラフ（コンテナツリー編集）、カーブエディタ、JSON 保存。
4. ~~**virtualization**~~ ✅ 完了（3D ループ向け）。`Kind.priority`/`virtualize`。上限超過時に低優先の実ボイスを仮想化して枠を作る/新規を仮想開始。`UpdateVirtualization` が枠の空きで高優先の仮想を実ボイスへ復帰（`DevirtualizeInstance`）。エディタに `virtual` 表示。※ 真の「再生位置を進める仮想化」（任意 startFrame 復帰）は将来。2D ループはストリームなので対象外。
5. ~~**動画連携（音声側）**~~ ✅ 完了。`SoundEngine::OpenPushStream` + `SoundStream::PushPCM`（プッシュ駆動）。`Sound/Video/VideoPlayer`（Media Foundation でメディアの音声トラックをデコード→プッシュ供給、`GetClock()`=media clock）。
   - **続き**: 動画**フレーム**のデコード/表示（MF で映像ストリーム→テクスチャ→レンダパイプライン統合）。A/V 同期は `VideoPlayer::GetClock()` を master に。
6. ~~**専用エディタ（第二段）**~~ ✅ 完了。Objects ツリー可視化（Random/Sequence/Switch/Blend 展開、Switch は現在値ハイライト、各ノード Play）。
   - 続き候補: ノードグラフ編集、カーブエディタ、JSON 保存。
7. **Android(Oboe) バックエンド** — 共有ソフトウェアミキサ / リサンプラ / SPSC。`MFDecoder`/`VideoPlayer`(MF) は Windows 専用なので Android は AAudio + MediaCodec へ差し替え。**NDK 必須・当環境ではビルド不可**。
8. **Ogg(stb_vorbis)** — MF で mp3/aac/wma/m4a を賄うため Windows では優先度低。クロスPF（Android）では要。stb_vorbis.c の取り込みが必要。

## 運用メモ
- サウンド変更は `feature/sound-system` に集約し main へマージ。別途進行中の Platform 抽象化/Xbox UWP 移植（`Engine.cpp`/`Platform/*` 等, `feature/xbox-uwp`）とは分離している。
- テスト: `Game/Assets/Audio/Main.audiobank.json` + 生成 WAV、Application のテストキー（P/B/3/M/E/G/F/T/1/2/7/9/0/C/X/Y/U/J/K/I/O/N）。
