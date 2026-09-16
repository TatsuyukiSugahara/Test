---
name: skybox-implementation
description: スカイキューブの実装で踏んだ 3 点 — 深度 LESS + クリア 1.0 の罠、invVP の平行移動除去、SkyRenderer の生成タイミング
metadata: 
  node_type: memory
  type: project
  originSessionId: 67329cf3-7e0e-4cab-b8c5-681ec8644ca3
  modified: 2026-09-11T08:20:18.377Z
---

2026-09-11 に `aqEngine/Rendering/Sky/` を追加(設計書 `Skybox設計.md`、コミット `af67223`)。
**キューブマップの読み込みは 4 バックエンドとも元から実装済み**だった
(`Resource.cpp` が DirectXTex の `IsCubemap()` から `Texture2DDesc::isCubemap` を立てる)。
新規に書いたのは描画側だけ。

**踏んだ 3 点:**

1. **深度比較は全バックエンドで `LESS` 固定なのに、G-Buffer の深度クリア値は 1.0**。
   空を `z = 1.0` で出すと `1.0 < 1.0` が偽になり**全画素が落ちて何も映らない**。
   VS の出力 z は `0.999999`。
   **既存のフルスクリーン VS(`PBRLighting.fx`)は `z = 0.0` を出している**ので、
   そのまま流用すると今度は空が最前面に来て全部隠す。深度無効パス専用。
2. **`invViewProjection` は view の平行移動成分(行ベクトル規約なので第 4 行)を 0 に
   してから合成する。** 抜かないとカメラ移動で空がずれて無限遠にならない。
3. **`SkyRenderer` の生成は `OnInitialize()` では動かない。** `ResourceManager::Load` を
   使うクラスは **`OnRegister()`(バンク登録)より後**でないと `EngineAssert` で落ちる。
   Engine は `Initialize()` → `Register()` の順に呼ぶ(`Engine.cpp:95-105`)。
   `DeferredRenderer` が `OnInitialize` で作れているのは `CreateShader` しか使わないから。

**その他:**
- 絵は `Tools/SkyCubeGen/gen_skycube.py` で手続き生成した DDS。ビルドには組み込まず
  生成物をコミットしている。**値ノイズの格子は整数座標でハッシュすること**
  (実数を直接ハッシュすると補間が効かず雲がフィルムグレインになる)。
- **ミップは `Resource.cpp` が読み込み時に自動生成する**(DDS 側は 1 枚でよい)。
- 新しい `.fx` は **`shader_entries.txt` への登録が必須**。忘れると Vulkan / Metal で
  シェーダが見つからない(D3D は実行時コンパイルなので気付きにくい)。

**未了(P3)**: 分割画面、ミニマップ、デバッグ UI トグル、太陽の向きをシーンの
ディレクショナルライトと合わせること。関連: [[mac-port-phase-status]]
