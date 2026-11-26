# ReflectionTest — リフレクションの検査ツール

コンポーネントの `Reflect` と、`ComponentMeta` の型消去関数(`serializePtr` / `deserializePtr`)、
編集イベント Visitor(`ImGuiEditEventVisitor`)とフィールド属性(`FieldAttr`)を、
画面もデバイスも作らずに検査するコマンドラインツール
(→ [設計書/エディタ全体設計.md](../../設計書/エディタ全体設計.md) 6.4 節「5. 検査ツール」、E6a / E6b)。
E4a で、編集ノード(`PrefabEditNode`)の JSON 往復(T6)と数値の書き出し(T7)の試験を足した
(同 7.8 節「9. 検査ツールの追加」)。E4b で、T6 に `LevelEditModel` 経由の Level 全体の往復を追加し、
`Engine`/`EntityContext` を画面無しで動かして Level の生成・取り消しを検査する T8 を足した
(同 7.8 節「9.」の T8 行と、節末尾の「T8 の実行環境」)。
E5a で、System のモード宣言(`RunMode` / `RunModeMask`)を検査する T9 を足した
(同 8.5 節「1.」)。
E7a で、`LevelEditModel` のノードの構造操作(挿入・取り外し・移動・名前・相対参照の判定)を検査する T10 を足した
(同 7.9 節「10.」)。
E7b で、`LevelEditModel` のコンポーネントの値・追加・削除と参照ノードの解決(基準の付け替え)を検査する T11 を足した
(同 7.9 節「8.」「10.」)。
E9a で、コンテンツブラウザのアセットの一覧(`AssetCatalog`)を検査する T15 を足した(同 7.10 節)。
E11a で、シーンビューの数学(`IntersectRayAABB`、`Camera` の `ScreenPointToRay` / `WorldToClip`)を検査する T16 (a) を足した(設計書/エディタ/E11_シーンビュー.md)。

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
| T6 編集ノードの往復(E4a はノード単位、E4b は Level 全体) | `TestData/` と `--assets` 以下の全 `*.level.json` の `entities` の各ノードと、全 `*.prefab.json` のルートを `PrefabNodeFromJson` → `PrefabNodeToJson` し、設計書 11.4 節の規則で元と比べる(参照ノードは `prefab` と `overrides` の生 JSON を構造で比べ、キーの有無も一致・数値は double で完全一致。実体ノードはコンポーネントを既定値で構築して比べ、子の順序と数も比べる。ノード名は実行時の名前 = 参照ノードは `overrides.name` 優先)。さらに `TestData/T6_Overrides.level.json` の参照ノード `RefFull` をプログラムで編集して書き出す: (a) override に無かったフィールド(Transform の `scale`)を変える → そのキーだけが増える、(b) 変えて元に戻す → 元と同じ、(c) override コンポーネント(Spawner)を外す → `components` から消える / 外して同じ型を足し直す → 全フィールドが `components` に入る、(d) `addedComponents` の AudioSource を変える → `addedComponents` 側が全フィールドで変わる、(e) Name を変える → 外側の `name` と `overrides.name` の両方が変わる、(f) `addedComponents` の AudioSource を外す → 書いて読み直しても復活しない、(g) 外して足し直して値を変える → 読み直すと新しい値で、`addedComponents` に古い値が残らない。**E4b で追加**: `TestData/T6_Overrides.level.json`(2 段のインライン subLevel、Inner1 の中に Inner2 を持つ)を `LevelEditModel::FromJson` → `ToJson` し、(1) 1 回目と、それを読み直してもう一度書き出した 2 回目が一致する(安定している)、(2) 入れ子の subLevel(Inner2)が捨てられずに残ることを確かめる。Debug 構成のみ(編集ノードが `AQ_DEBUG_IMGUI` のときだけある) |
| T7 数値 | `Stringify` の表記と、読み直した値を確かめる(下の表)。Debug 構成では、`TestData/T7_Numbers.level.json` を編集ノードへ読み込んで書き出したファイルを一時ディレクトリへ書いて読み直し、7 桁以上の小数を持つ通常のフィールドは float として、override の値は double として一致することも確かめる |
| T8 生成と取り消し(E4b) | 画面無しで `Engine::Create()` + `ecs::EntityContext::Initialize()` を使い(ウィンドウ・デバイス・ThreadPool・AppHost・MemoryManager は作らない)、Transform だけの試験用 Level で: (a) `Load` 直後は `IsSpawned` が false、`FlushPendingCommands` 後は true。(b) `Load` → `Unload` → Flush でエンティティが 0(同期ロードの不具合の修正確認)。(c) `LoadAsync` 中に根を `Unload` すると `IsCancelled`/`IsDone` が true になり、所属の無いエンティティが残らない。(d) `LoadAsync` 中に子だけを `Unload` すると、子は作られず親は最後まで作られる。(e) `Register` → `Unregister` → `IsRegistered` が false。(f) `RequestPreview` を 10 フレーム連続で呼んでも、エンティティ数が毎フレーム同じで、プレビューの登録が 1 つだけ(Debug 構成のみ)。Debug 構成のみ(`LevelEditModel` が `AQ_DEBUG_IMGUI` のときだけあるため (f) は Debug 限定。(a)〜(e) は Release でも動く) |
| T9 実行モード(E5a) | T8 と同じく画面無しで `Engine::Create()` + `ThreadPool::Initialize` + `ecs::EntityContext::Initialize()` を使い(T8 の後始末の後に作り直す。System は `FinalizeRegistration` の後に足せないため)、試験用の System 4 種(`GetRunModes` を上書きしない・`Play`・`All`・`Edit \| Play` の宣言)を登録して、`EntityContext::Update(RunMode::Edit / Play / Pause)` を 1 回ずつ呼ぶ。各モードで: (1) System ごとの `Update` の回数が宣言どおり(`Edit`: All と Edit\|Play の 1 回ずつ、`Play`: 4 種とも 1 回、`Pause`: All だけ)。(2) `RequestCreateEntity` で積んだ生成が、`Update` の前は反映されず、`Update` の後(= Flush の後)に 1 つ増える(Flush はモードに関係なく行う)。Release でも動く |
| T10 構造操作(E7a) | 画面も `Engine` も作らず、コード内の試験用 Level(トップレベル 3 つ(実体 2・参照ノード 1)、子と孫、インラインの subLevel 1 つ、ファイル参照の subLevel 1 つ)を `LevelEditModel` へ読み込んで試す。(a) `InsertNode`(トップ・子・subLevel のルート・範囲外の index)で ID が振られ `ToJson` の位置に出る。採番済みの ID は変えず、参照ノードの下・ファイル参照の subLevel・存在しない親への挿入は 0 を返してモデルを変えない。(b) `DetachNode` で外したサブツリーは子孫の `editId` を保ち、`FindParent` で得た元の置き場所へ戻すと `ToJson` が元と一致し、同じ ID で `FindNode` できる。(c) `MoveNode` でトップ ↔ 子 ↔ subLevel のルートへ子孫ごと移る(同じ列の中で後ろへ動かすときは取り外した後の添字)。(d) 自分・子孫・参照ノード・ファイル参照の subLevel への移動は false でモデルが変わらない。(e) `SetNodeName`。(e') `HasRelativeReferences` は、相対の `prefab` / subLevel の `path`(トップレベル・子孫・インライン subLevel・入れ子を含む)があるときだけ true で、絶対パス(`/` 始まり・`X:`)だけなら false。(f) 操作の後に `ToJson` → `FromJson` → `ToJson` が一致する。Debug 構成のみ(`LevelEditModel` が `AQ_DEBUG_IMGUI` のときだけあるため) |
| T11 値の操作(E7b) | `TestData/T6_Overrides.level.json`(参照ノード `RefFull` / `RefPlain`、基準ディレクトリは TestData)と `T6_Base.prefab.json` を `LevelEditModel` へ読み込んで試す。(a) 実体ノードの `SetComponentField`(float・Vector3・パス)で、そのキーだけが変わり、ほかのノードは変わらない。(b) 参照ノードで override の無いコンポーネント(`RefPlain` の Spawner)のキーを変えると、`overrides.components.<型>` にそのキーだけが増える。(c) override のあるコンポーネント(`RefFull` の Transform)では、そのキーだけが足され、既存の override は一字一句変わらない。(c') 基準の付け替え: Prefab で 3 のキー(`maxCount`)を既定値と同じ `-1` にするとそのキーが `-1` で書かれ、`3 → 5 → 3` は何も増えない。override の疎な JSON にあるキー(`interval`)を持つコンポーネントで別のキーを変えても既存のキーは変わらない。(d) 変えて元に戻すと(実体ノードも参照ノードも)Level 全体の `ToJson` が元と一致する。(e) `ResolveReferenceComponents` が Prefab 側の値・override の値・追加・除去・編集中の値を反映する。参照ノードでない・直接の参照先が無いと false(error に理由)、コンポーネントを持たない Prefab は true で空。(f) `AddComponent` / `RemoveComponent`(既に持つ・未登録・Transform・持たない型・参照ノードは false で、外す前の値が `outJson` に入る)。(g) 値を変えた Level(参照ノードは TestData の Prefab を絶対パスで指す)を一時ファイルへ `SaveFile` し、変えたキーだけが書かれていることを確かめてから、`LevelSerializer::Load` → `LevelRegistry::Register` → `LevelManager::Load` → Flush で、変えた値(Transform の位置・Spawner の `maxCount`)が実体のコンポーネント(`serialize`)に出る。(g) だけは T8 と同じ `Engine::Create()` + `ecs::EntityContext::Initialize()` の環境で行い、一時ファイルは終わりに消す。(h) 失敗する `SetComponentField`(未登録の型・持たない型・参照先から除かれた型・参照先に無い型・無い ID・参照先が無い参照ノード)は false で、モデルを変えない。Debug 構成のみ |
| T15 アセットの一覧(E9a) | Editor の `AssetCatalog.cpp` を直接コンパイルして試す。(a) `ClassifyAsset` の 15 例(大文字の `.DDS`・`.Prefab.JSON`、`.json` だけ、拡張子なし、フォルダ名に拡張子を含むパスなど)、`GetKindTag`、`TestData/T15_Assets/` を根にした `AssetCatalog` の木の形・フォルダとファイルの並び(大文字小文字を無視)・種類・`.` で始まるものが出ないこと・`FindFolder`・読み直し・根が無い / ファイルのとき。全構成。**E9b で追加(b)**: Editor の `PrefabAuthoring.cpp` と `NodeCommands.cpp` も直接コンパイルして試す(Debug のみ)。`SanitizeFileName`(禁止文字・制御文字・前後の空白・空)、`FindFreePrefabPath`(一時フォルダに既存ファイルを置いて `_1` `_2`)、`ReferenceNameFromPath`、`MakeReferencePath`(`baseDir` 空 / 同じフォルダ / 兄弟フォルダ `../Prefabs/X.prefab.json` / 絶対パス)、`RebaseReference`(相対・絶対)。`TestData/T15_Prefab.level.json` を読み、`Group` から `CreatePrefabFromNode` で一時フォルダの下の `sub/` へ Prefab を書く: 書いた JSON の `prefab`(`overrides.children` の中も)が `sub/` からの相対に書き換わっていること、`PrefabSerializer::Load(書いたファイル)` と元の `Group` の JSON を Level の基準ディレクトリで解決した `PrefabData` がルートから全子孫まで同じこと(参照先が欠けていないことを `Spawner` と子の有無でも確かめる)、参照ノードそのもの・無い ID は false でファイルを作らないこと、既存のファイルは上書きしないこと。`ReplaceWithReferenceCommand` の実行・取り消し・やり直しで `ToJson` と ID(子孫・参照ノード)が戻ること、参照ノードへの適用は false でモデルが変わらないこと、`CreateReferenceNodeCommand` の実行・取り消し・やり直しと、参照ノードの下・無い親への作成が false であること。一時フォルダは試験の終わりに消す(macOS の一時フォルダは `/var` が `/private/var` へのリンクなので、字句だけで作る `../` の列がずれないよう `canonical` にしてから使う) |
| T16 シーンビューの数学(E11a) | 画面も `Engine` も作らず、(a) `Math/Bounds.h` の `IntersectRayAABB`(正面・斜め・横へ外れ・始点が箱の中で距離 0・軸に平行で範囲の中 / 外・後ろ向き・箱の後ろ・上から・空の箱、外れたとき距離を変えないこと)と、`Camera` の `ScreenPointToRay` / `WorldToClip` の往復(画面の中央・四隅など 7 点で、レイの上の点を `WorldToClip` へ写して w で割ると元の正規化座標へ戻る。許容 1e-4。向きの正規化、中央のレイが前方向で始点が近平面上であること、手前の点の z が負であること)。全構成 |

T7 の表記の期待値(設計書 7.8 節「1. 数値」の 3 分岐: 整数で絶対値が 2^53 未満 → 小数点なし、
float に狭めても同じ値 → float の最短表現、それ以外 → double の最短表現):

| 入力 | 表記 | 読み直しの比較 |
|---|---|---|
| `0.1f` を double に広げた値 | `0.1` | float |
| `1234567.5` | `1234567.5` | double |
| `16777217` | `16777217` | double |
| `2147483647` / `-2147483648` | 同じ | double |
| `9007199254740992`(2^53)/ `9007199254740993`(double では 2^53) | `9.007199e+15` | float(2^53 は「2^53 未満」に当たらず、float に狭めても同じ値なので float の最短表現になる) |
| `1e20` | `1e+20` | double |
| `1e-05` | `1e-05` | double |
| `-0.0` | `0` | double(`==` で比べる) |

### 試験用データ(`TestData/`)

| ファイル | 中身 |
|---|---|
| `T6_Base.prefab.json` | 参照ノードが指す Prefab(子 `ChildA` / `ChildB`、Spawner、AudioListener) |
| `T6_Overrides.level.json` | 実体ノード(子付き)、`overrides` の `name` / `components` / `addedComponents` / `removedComponents` / `children` / `removedChildren` をすべて持つ参照ノード `RefFull`、overrides の無い参照ノード、2 段のインライン subLevel(E4b 用) |
| `T6_Nested.prefab.json` | 入れ子の参照ノード(子の override を持つ)を持つ Prefab(Prefab エディタの経路) |
| `T7_Numbers.level.json` | 7 桁以上の小数を持つ float のフィールドと、float に狭めると変わる override の値 |
| `T15_Assets/` | T15 の `AssetCatalog` の根にする小さな木(フォルダ 2 段、各種類のファイル、`.` で始まるファイルとフォルダ)。中身は空か `{}`。T6 の往復の対象からは外す |
| `T15_Prefab.level.json` | T15 (b) の Prefab 作成の試験用 Level。実体ノード `Group`(Transform あり)の子に、`T6_Base.prefab.json` の参照ノード `RefPlain`、`overrides.children` に `T6_Base.prefab.json` を参照する子 `Extra` を足した参照ノード `RefExtra`、実体の子 `Solid`(孫 `SolidChild` 付き)。T6 の往復の対象にも入る |

場所は `CMakeLists.txt` がソースの位置(`REFLECTION_TEST_DATA_DIR`)を埋め込むので、カレントディレクトリに依らない。

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

E4a の前に T6 / T7 を今のコードで足し、次の 18 件を `[XFAIL]` として控えてから直した
([エディタ全体設計](../../設計書/エディタ全体設計.md) 7.8 節「1. 数値」「2. override の保持」):

| 試験 | 直す前の不具合 |
|---|---|
| `T6 TestData/T6_Overrides.level.json entities[1]`、`T6 TestData/T6_Nested.prefab.json ルート`、`T6 TestData/T7_Numbers.level.json entities[1]` | `PrefabNodeFromJson` が `overrides` の `name` / `addedComponents` / `children` / `removedChildren` を捨て、`PrefabNodeToJson` が override のコンポーネントを全フィールド書く(既定値のキーが増え、子の override が消える) |
| `T6 (a)`〜`T6 (g)` の 8 件 | 同上(`addedComponents` のコンポーネントが読み込まれない、Name が `overrides.name` に効かない) |
| `T7 Stringify 1234567.5`、`16777217`、`2147483647`、`9007199254740992`、`9007199254740993` | `Stringify` が数値を float に狭めてから `%g`(有効 6 桁)で書く |
| `T7 ファイル往復 float のフィールド`、`T7 ファイル往復 override の double` | 同上(7 桁以上の小数が変わる。override の double が float に狭まる) |

---

## 4. ビルド

CMake 専用(`DirectX/CMakeLists.txt` の開発用ツールの節。Android / iOS では配線しない)。
vcxproj は用意していない。`aqEngine` にリンクする(試験のためにエンジンのコードを変えない)。
