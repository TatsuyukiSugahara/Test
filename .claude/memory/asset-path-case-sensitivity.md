---
name: asset-path-case-sensitivity
description: tkm は拡張子を小文字 .dds へ置換するがアセットは .DDS。大文字小文字を区別する OS でだけテクスチャが落ちる
metadata:
  type: project
---

`Resource.cpp` の tkm マテリアル読み込みは、参照テクスチャの拡張子を
**小文字 `.dds` へ機械的に置換**する(`ReplaceExtension(ResolveSiblingPath(...), ".dds")`)。
一方、同梱アセットの実体は **`utc_all2.DDS` のように大文字**。

**Windows / macOS のボリュームは既定で大文字小文字を区別しないため何年も表面化しなかった。**
**iOS(シミュレータ・実機とも)と Android の内部ストレージ(ext4)は区別する**ので、
そこでだけ**キャラクタのテクスチャが 1 枚も開けず、モデルが灰色になる**。

**症状が分かりにくい理由**: `LoadFromDDSFile` / `LoadFromTGAFile` は
**失敗してもログを出さない**(WIC / stb_image 経路は出す)。
「モデルは出ているのに色だけおかしい」としか見えない。切り分けには
`LoadImageFile` の入口にパスと成否を出す一時ログを入れるのが速い。

**対処(2026-09-14 実装済み)**: `BuildResourcePathCandidates` に
`PushExtensionCaseVariants()` を追加し、**完全一致の候補をすべて並べた後に**
拡張子を小文字化/大文字化した候補を足す。絶対パスの早期 return 経路にも適用している
(tkm のマテリアルは解決済み絶対パスを基点に組み立てられるため、ここを素通りすると効かない)。

**How to apply:** 新しいアセットは**拡張子を小文字で統一**する。
既存アセットを小文字へ揃える片付けは未実施(`iOS移植設計.md` §8-12)。
非 Windows でテクスチャだけ出ないときは真っ先にここを疑う。
