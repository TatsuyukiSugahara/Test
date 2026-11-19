# ReflectionTest — リフレクションの検査ツール

コンポーネントの `Reflect` と、`ComponentMeta` の型消去関数(`serializePtr` / `deserializePtr`)、
編集イベント Visitor(`ImGuiEditEventVisitor`)とフィールド属性(`FieldAttr`)を、
画面もデバイスも作らずに検査するコマンドラインツール
(→ [設計書/エディタ全体設計.md](../../設計書/エディタ全体設計.md) 6.4 節「5. 検査ツール」、E6a / E6b)。

E6a でエンジンを変える前に現状の出力(T3)を控え、E6b の変更の前後で既存アセットの保存結果が
変わらないことを機械的に比べるために使う。

---

## 1. 使い方

```
ReflectionTest --selftest [--assets <Game/Assets のパス>] [--dump <出力ファイル>]
```

| オプション | 意味 |
|---|---|
| `--selftest` | 試験を行う(必須) |
| `--assets <path>` | T3 で走査するディレクトリ。無ければ T3 は行わない |
| `--dump <path>` | T3 の結果を書くファイル。無ければ件数だけ出す |

Mac の例(`DirectX/` で):

```
cmake --build build/macos-ninja-metal --config Debug --target ReflectionTest
build/macos-ninja-metal/bin/Debug/ReflectionTest --selftest --assets Game/Assets --dump t3.txt
```

- 1 試験 1 行で `[PASS]` / `[FAIL]` / `[XFAIL]` / `[XPASS]` を出す。失敗した行には違うキーを続けて出す。
- `[XFAIL]` は既知の失敗(下の「既知の課題」)。終了コードには数えない。
- `[FAIL]` か `[XPASS]` が 1 つでもあれば終了コード 1、無ければ 0。
  `[XPASS]` は既知の失敗の印が付いた試験が通ったことを表す。直ったのに印を外し忘れないため失敗扱いにする。
- 試験の対象外の型(`serializePtr` / `deserializePtr` が無い型)は、印を付けずに名前だけを出す。
- T4 / T5 は ImGui の Visitor(`AQ_DEBUG_IMGUI`)が要るので Debug 構成だけで行う。Release では行わない旨の 1 行だけを出す。
- T4 は、実際に出た編集イベントの列を `f<フレーム> <段階> <キー> <値>` で試験の行の前に出す(合否の確かめと記録を兼ねる)。
- 最後に `[selftest] PASS n / FAIL n / XFAIL n / XPASS n` を出す。

---

## 2. 試験

コンポーネントは `ComponentRegistry::RegisterCoreComponents()` と
`level::RegisterLevelComponents()` で登録し(`Application::Initialize` と同じ)、
実体は `AlignedStorage(TypeInfo)` で既定値のまま作る。

| 試験 | 内容 |
|---|---|
| T1 往復 | `serializePtr` を持つ全型で、既定値 → `serializePtr` → 新しい実体へ `deserializePtr` → `serializePtr` が一致する |
| T2 疎な JSON | 全型で、`{}` を `deserializePtr` しても書き出しが変わらない。数値・真偽値・数値の配列の各キーについて、そのキーだけの JSON(数値は +1、真偽値は反転、配列は先頭の要素に +1)を `deserializePtr` すると、書き出しでそのキーだけが変わり、値が一致する。パス(文字列)のキーは変えない(リソースを読ませないため)。Animation はクリップを 1 つ持たせた実体(`{"clips":[{"name":"a","path":"x.fbx"}]}` を `deserializePtr` して作る)でも `{}` と `playSpeed` だけの JSON を試す |
| T3 既存アセット | `--assets` 以下の全 `*.prefab.json` と `*.level.json` を開き、JSON 中の `components` オブジェクトを再帰で探す。各コンポーネントを既定値の実体へ `deserializePtr` してから `serializePtr` し、結果を `--dump` へ書く。最後にファイル数・コンポーネント数を出す。JSON として読めないファイルがあれば `[FAIL]` |
| T4 編集イベント | 画面を持たない ImGui のコンテキストに入力を合成し、試験用の型(float、片側だけ範囲付きの float、bool、Vector3、Quaternion、パス、文字列、readOnly、hidden)を `ImGuiEditEventVisitor` で描く。(1) float のドラッグで Begin(変更前)→ Update 1 回以上 → Commit(変更後)。(2) 押して動かさずに離すと Begin → Cancel。(3) チェックボックスで Begin → Update → Commit、値が反転する。(4) Vector3 の 1 成分のドラッグで Begin → Update → Commit が 1 組。(5) パスと文字列は、値を変えて Enter で Begin と Commit が同じフレームに 1 組、同じ値の Enter では何も出ない(`FieldPath` の戻り値も「確定し、かつ値が変わった」ときだけ true)。(6) readOnly と hidden はイベントを出さず、hidden は `itemRects` に出ない。(7) Vector3 を描く直前に `ImGuiContext::AnyIdHasBeenEditedThisFrame` を立てても Update が出る。(8) ドラッグ・Ctrl+クリックの直接入力の途中で描くのを 2 フレームやめて再び描いても、古い操作の Commit / Cancel が出ず、出るなら同じフレームの Begin と Commit の 1 組。(1)〜(7) では Begin の後に Commit か Cancel が必ず 1 つ来る。ほかに、DragFloat3 で部品を Tab で移ったときの列を記録だけする(合否に使わない) |
| T5 属性 | `ImGuiFieldVisitor` と `ImGuiEditEventVisitor` の両方で、下限付き(`Min(0.5f)`)の float を下限の外へドラッグしても、Ctrl+クリックで `-5` を入力しても 0.5 に収まる。範囲の無い float は同じ操作で制限されない |

- 数値は float に丸めて比べる。
- T2 の試験名は `T2 <型名> <キー>`(`{}` の試験は `T2 <型名> {}`)。
- T4 / T5 の入力は 1 種類ずつ 1 フレームに入れる(ImGui の入力キューは、同じフレームの押下と移動などを
  フレームをまたいで細切れにするため)。項目の位置は `ImGuiEditEventVisitor::itemRects` から取る。
  試験の区切りでは 30 フレーム(0.5 秒)空ける。前の試験と同じ位置を続けてクリックすると
  ダブルクリック(= 直接入力)になるため。
- 画面無しの ImGui はレンダラーを持たないので、`ImFontAtlas::GetTexDataAsAlpha8` で
  フォントアトラスを自分で作る(`ImGuiBackendFlags_RendererHasTextures` を立てない旧来の経路)。
  `io.ConfigMacOSXBehaviors` は false にして、Ctrl を Mac でも Ctrl として扱う。

### T3 の出力形式

1 コンポーネント 1 行、タブ区切り。

```
<--assets からの相対パス>	<JSON の位置>	<型名>	<書き出し結果>
```

- JSON の位置はキーの道筋(例: `entities[0].children[1].components`)。
- 書き出し結果は `JsonSerializer::Stringify` の改行と字下げを詰めて 1 行にしたもの。
- 登録されていない型は `(未登録)`、`serializePtr` が無い型は `(serializePtr なし)` とだけ書く。
- ファイルは相対パス順、オブジェクトのキーは名前順に並べる(出力を決定的にし、`diff` できるようにする)。

---

## 3. 既知の課題

現在、既知の失敗(`[XFAIL]`)は無い。`Main.cpp` の `KNOWN_FAILURES` は空。

新しく見つけた不具合をすぐに直さないときは、`KNOWN_FAILURES` に試験名を足して `[XFAIL]` にし、
ここに表で記録する(直して通ると `[XPASS]` で終了コード 1 になるので、そのとき印と行を消す)。

E6a で記録していた 5 件(`T2 Animation(クリップ付き) {}`、`T2 Animation(クリップ付き) playSpeed`、
`T2 Terrain resolution`、`T2 Terrain heightScale`、`T2 Terrain terrainSize`)は E6b で直した
(Animation の疎な JSON、Terrain の型消去の経路での往復。
[エディタ全体設計](../../設計書/エディタ全体設計.md) 6.4 節の例外 1・例外 3 と「0. 既存の不具合の修正」)。

---

## 4. ビルド

CMake 専用(`DirectX/CMakeLists.txt` の開発用ツールの節。Android / iOS では配線しない)。
vcxproj は用意していない。`aqEngine` にリンクする(試験のためにエンジンのコードを変えない)。
