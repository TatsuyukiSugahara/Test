# サウンドシステム 残作業（続き）

最終更新: 2026-06-29 / ブランチ `feature/sound-system`

低レベル `SoundEngine`（XAudio2）＋ データ駆動オーサリング層（`Sound/Authoring/`）は実装済み。
詳細設計は [Sound設計.md](Sound設計.md) / [AudioAuthoring設計.md](AudioAuthoring設計.md)、現状サマリは Sound設計.md §14。

## 実装済み
- SoundEngine: 2D ワンショット / ストリーミング(BGM・背圧・ループ) / 3D(`Mixer3D`: 距離減衰・パン・ドップラー) / バス(Master/BGM/SE/Voice) / 音量・ピッチ・フェード / 全体&バス単位 Pause・Stop
- リソース: `SoundClip` + 拡張子ディスパッチ。WAV / WAVストリーム / Media Foundation(MP3/AAC/WMA/M4A)
- ECS: `AudioListenerComponent` / `AudioSourceComponent` / `SoundSystem`
- オーサリング層: Event / Kind / Sound / Random / Sequence / Switch / 3D(`AudioEventEmitterComponent`) / RTPC / State+stateRules / 自動ダッキング、ImGui パネル `AudioAuthoringPanel`

## 続き（未実装・優先度順の目安）
1. **音量の調停** — ダッキング/RTPC が `SetBusVolume`/`SetVolume` を毎フレーム上書きし、ユーザーの `FadeBus`/フェードと競合する。`base × duck × RTPC` を合成する調停レイヤを入れる（既存機能の正しさに直結）。
2. **Blend コンテナ** — 複数レイヤ同時再生 + RTPC でレイヤ音量制御。
3. **virtualization** — 実ボイス無しで再生位置だけ進める真のボイス仮想化（低レイヤ拡張が必要）。
4. **専用オーサリングエディタ** — ImGui パネルの発展（ノードグラフ / カーブ編集 / 解決結果の可視化）。
5. **Android(Oboe) バックエンド** — 共有ソフトウェアミキサ / リサンプラ / SPSC。`MFDecoder` は Windows 専用なので Android は MediaCodec 等へ差し替え。
6. **動画連携** — `VideoPlayer`(FFmpeg/MediaCodec) → `SoundStream` へ push。A/V 同期は `SoundStream::GetPlaybackClock()`。
7. **Ogg(stb_vorbis)** — MF で mp3/aac/wma/m4a を賄うため Windows では優先度低。

## 運用メモ
- このブランチのコミットには、別作業の未コミット変更（`aqEngine/aq.h` のグラフィクスバックエンド選択リファクタ、`Graphics/Vulkan/*`、`Game/imgui.ini`）は含めていない。
- テスト: `Game/Assets/Audio/Main.audiobank.json` + 生成 WAV、Application のテストキー（P/B/3/M/E/G/F/T/1/2/7/9/0/C/X/Y）。
