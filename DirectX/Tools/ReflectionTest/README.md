# ReflectionTest — リフレクションの検査ツール

コンポーネントの `Reflect` と、`ComponentMeta` の型消去関数(`serializePtr` / `deserializePtr`)を、
画面もデバイスも作らずに検査するコマンドラインツール
(→ [設計書/エディタ全体設計.md](../../設計書/エディタ全体設計.md) 6.4 節「5. 検査ツール」、E6a)。

E6b でエンジンを変える前に現状の出力を控え、変更の前後で既存アセットの保存結果が
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

- 数値は float に丸めて比べる。
- T2 の試験名は `T2 <型名> <キー>`(`{}` の試験は `T2 <型名> {}`)。

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

`[XFAIL]` として扱う既知の失敗。印は `Main.cpp` の `KNOWN_FAILURES` に名前で持つ。
直したら(E6b)該当する行を消す。

| 試験 | 不具合 |
|---|---|
| `T2 Animation(クリップ付き) {}`、`T2 Animation(クリップ付き) playSpeed` | Animation の疎な JSON |
| `T2 Terrain resolution`、`T2 Terrain heightScale`、`T2 Terrain terrainSize` | Terrain の型消去の経路での往復 |

原因と直し方は [設計書/README.md](../../設計書/README.md) の「既知の課題」と、
[エディタ全体設計](../../設計書/エディタ全体設計.md) 6.4 節(例外 1・例外 3)が一次資料。

---|---|
| `T2 Animation(クリップ付き) {}`、`T2 Animation(クリップ付き) playSpeed` | `AnimationComponent::DeserializeFrom` が先頭で `slots_.clear()` するので、`clips` の無い JSON を渡すとクリップが消える(設計書 6.4 節の例外 1) |
| `T2 Terrain resolution` | `TerrainComponent::Reflect` の `resolution` は、書き出しが `chunk_.GetResolution()`、読み込み先が `authorResolution_` で別の変数。型消去の経路では `OnDeserialized` が呼ばれないので往復しない(例外 3) |

---

## 4. ビルド

CMake 専用(`DirectX/CMakeLists.txt` の開発用ツールの節。Android / iOS では配線しない)。
vcxproj は用意していない。`aqEngine` にリンクし、エンジンのコードは変えない。
