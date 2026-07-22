# Prefab データ駆動化 設計（改訂4）

> 改訂3 → 改訂4 の変更（再レビュー反映）:
> - **High**: `ConstructFn` は trivial 型でも nullptr にせず**全型で必須**（Chunk は生メモリ、構築省略は未初期化バグ）。
> - **High**: 遅延 `Instantiate` コマンドの**寿命ルールを確定**（`this`/参照を捕まえない。`shared_ptr<const PrefabData>` を捕獲）。
> - **Medium**: 型消去ストレージの**アライメント保証**と **move-only / CopyFn** を明記。
> - **Medium**: ランタイム表現を**「JsonValue ベースのプラン」に固定**（型消去実体はエディタ専用）。揺れを解消。
> - **Medium**: `PrefabId` は **JSON 上は path/GUID 文字列が正本**、uint64 はランタイムキャッシュキーのみ。衝突検出＋診断。
> - **Low**: requiredWith 展開後の **dedup と MAX_COMPONENT_SIZE 超過の診断エラー化**を Phase 1 に明記。

> 📖 **使い方（作成・生成・Component から Prefab を参照する方法）は末尾の [§11 使い方](#11-使い方usage--クイックスタート) を参照。**

## 1. 目的と背景

`Component/Prefab.h`（旧実装） の `Prefab` はコード版スポナー（構成がコンパイル時固定・初期値ラムダ・保存/読込/参照なし）。
これを **データ（JSON）駆動の設計図**へ作り直す。主要ユースケースは **ゲーム実行中の動的生成**（弾・敵・エフェクトを System から spawn）。

## 2. 既存資産と確認済みの制約

| 資産 | 場所 | 備考 |
|---|---|---|
| 中央レジストリ（TypeInfo→meta） | [ComponentRegistry.h](../aqEngine/ECS/ComponentRegistry.h) | `vector<pair<TypeInfo, meta>>`。名前→TypeInfo 解決の土台 |
| 既存 Inspector / Add パレット | [SceneHierarchySystem.cpp:242](../aqEngine/ECS/SceneHierarchySystem.cpp#L242) | `GetAll()`→`meta.has/add/drawInspector(handle)`。エディタで再利用 |
| 実行時 Archetype 構築 | [Archetype.h:97](../aqEngine/ECS/Archetype.h#L97) `AddType(const TypeInfo&)` | **重複を弾かない・上限超過は無言無視**（→ dedup/診断は呼び出し側責務） |
| 遅延 Entity 生成 | [EntityManager.h:77](../aqEngine/ECS/EntityManager.h#L77) `RequestCreateEntity` | コマンド実行時に「生成→**全構築 `new(...) Ts()`**→onCreated」。onCreated 内で GetComponent 有効 |
| コンポーネント構築 | [EntityManager.h:320](../aqEngine/ECS/EntityManager.h#L320) `NewComponent` | **triviality 無関係に常に placement-new**。Chunk は生メモリ |
| アライン確保 | [Chunk.h:164](../aqEngine/ECS/Chunk.h#L164) `AllocBuffer` | `operator new(bytes, align_val_t(align))`。型消去ストレージも同方式が必要 |
| JSON | [../Util/SimpleJson.h](../aqEngine/Util/SimpleJson.h) | パーサ/シリアライザ + `Merge`。コメントは `//` 行のみ |
| リフレクション宣言 | 各コンポーネントの `Inspect<V>` | 全て `#ifdef AQ_DEBUG_IMGUI` 内・ImGui 専用ロジック混在 → `Reflect` へ書き直し |

### ⚠ 確認済みの制約

1. `AddComponent<T>` は遅延コマンド → 「1 コマンド内で生成→構築→deserialize 連続実行」で回避。
2. `TypeInfo` に既定構築の関数ポインタが無い → **`ConstructFn` 追加（全型必須）**。
3. `Inspect` は全コンポーネントで `#ifdef AQ_DEBUG_IMGUI` 内 + ImGui 専用ロジック混在 → `Reflect` per-component 書き直し。
4. ラベルが UI 文言 → 永続キーと表示ラベルを分離。
5. `JsonValue::Merge` は add/override のみ → 削除は明示マーカー、children は `name` 同定。
6. `SimpleJson` のコメントは `//` 行のみ。
7. UI loader の `ref` は循環検出なし・無言フォールバック → 踏襲せず診断エラー化。
8. `Archetype::AddType` は重複/上限を防がない → **呼び出し側で dedup + 上限診断**。

## 3. 決定事項

- **配置**: `Component/` → `ECS/`。2 層（`ECS/Prefab`=ロジック / `ECS/PrefabSerializer`=JSON 層）。
- **生成方式**: 遅延がデフォルト。即時版は init/エディタ用の任意 API。
- **ランタイム Prefab 表現**: **`JsonValue` ベースの不変プラン**（`PrefabData`）に固定。型消去コンポーネント実体は**エディタ専用**（§6）。
- **リフレクション名**: `Inspect` → `Reflect`。
- **旧 API**: 段階的に廃止。

## 4. 生成 primitive（遅延デフォルト）

### 4.1 TypeInfo に既定構築を追加（全型必須）

```cpp
// TypeInfo.h
using ConstructFn = void(*)(void*);                       // ★追加
template <typename T> static void ConstructImpl(void* p) { new(p) T(); }
// Create<T>(): construct_ は【常に】&ConstructImpl<T>（trivial でも nullptr にしない）。
//   理由: Chunk は生メモリで、NewComponent は triviality 無関係に placement-new している。
//   構築を省くと trivial メンバ（float/int/bool）が未初期化になりオブジェクト寿命も曖昧化。
// DestructorFn は従来どおり trivial なら nullptr で可。
```

### 4.2 実行時型集合からの遅延生成 API

```cpp
// ForEach / System Update 内から安全に呼べる。実体生成は次の FlushCommands。
void EntityContext::RequestCreateEntityFromTypes(
    std::vector<TypeInfo>        types,
    std::function<void(Entity)>  onCreated);   // 構築後に呼ばれる。ここで deserialize
```

コマンド実行時の流れ（`isInUserCallback_` が立つ前）:

1. **型リスト確定**:
   - **dedup**（`AddType` は重複を弾かないため、ここで TypeInfo を重複除去）
   - **上限チェック**: dedup 後の件数 > `MAX_COMPONENT_SIZE` なら **生成中止 + 診断エラー**（無言で切り捨てない）
2. `Archetype a; for (t : types) a.AddType(t);`
3. `Entity e = CreateEntityImpl(a);`
4. 各 TypeInfo の `ConstructFn` で **全コンポーネントを placement-new**
5. `onCreated(e)`（GetComponent 有効）

> **層の責務分離（実装方針）**: `requiredWith` の推移展開と `EntityDebugTag` 注入は **primitive の外側**で行う。
> - `EntityDebugTag` 注入 … `EntityContext::RequestCreateEntityFromTypes`（debug のみ）。
> - `requiredWith` 補完 … ComponentRegistry に依存するため **Prefab/レジストリ層**（Phase 3/4）の責務。
>   ECS コア（`EntityManager`）を registry に結合させない。primitive 自体は完全な型リストを受け取り
>   dedup + 上限診断 + 全構築のみを行う。

> **実装状況（Phase 1 完了）**: 上記 primitive を実装済み。
> - `TypeInfo`: `ConstructFn`（全型必須）+ `Construct()` … [TypeInfo.h](../aqEngine/ECS/TypeInfo.h)
> - `Chunk::GetComponentByType(loc, type)` … [Chunk.h](../aqEngine/ECS/Chunk.h)
> - `EntityManager::RequestCreateEntityFromTypes` / `CreateEntityFromTypesNoLock` / `DedupTypes` … [EntityManager.h](../aqEngine/ECS/EntityManager.h)
> - `EntityContext::RequestCreateEntityFromTypes`（DebugTag 注入）… [EntityContext.h](../aqEngine/ECS/EntityContext.h)
> - 検証テスト（dedup + onCreated 時点の全構築）… BattleScene 初期化ブロック内
> - Debug|x64 ビルド成功（エラー/警告なし）。

### 4.3 ツリー（階層）の遅延生成と寿命ルール（★重要）

ツリー全体を **1 コマンド**で生成する。**遅延コマンドが捕獲してよいのは「値として安定した生成プラン」だけ**。

- **禁止**: `this` / `PrefabNode&` / `Prefab&` 等の参照・ポインタ捕獲。
  `PrefabSerializer::Load(...).Instantiate()` の一時オブジェクトや、Registry のリロード/アンロード後に
  Flush されると **use-after-free**。
- **採用**: コマンドは **`std::shared_ptr<const PrefabData>`**（不変・共有の生成プラン）を値で捕獲する。
  これにより Flush 時まで対象 Prefab の生存が保証される（一時 Prefab・Registry アンロードに耐える）。
  - `Prefab` は内部に `std::shared_ptr<const PrefabData>` を保持する薄いハンドル。
  - `PrefabSerializer::Load(...)` は `Prefab`（= shared_ptr 保持）を返す。
  - PrefabId 経由のスポーンも、enqueue 時に `Registry.Find(id)` で **shared_ptr を取得して捕獲**（flush 時の再解決はしない）。

コマンド実行時、そのアクション内（user-callback 前なので構造変更可）で `PrefabData` を再帰的に辿り、
ルート→子→孫を CreateEntityImpl + 構築 + deserialize（JsonValue から）+ HTC 親子付け。
完了通知が要れば末尾で `onComplete(rootEntity)`（ここだけ `isInUserCallback_`）。

即時に root ハンドルが要る init/エディタ用に `InstantiateImmediate(parentHandle)`（ForEach 外限定）を任意提供。

## 5. リフレクション（serialize/deserialize 基盤）

> **実装状況（Phase 2 一部完了）**: 仕組みを実装し単一エンティティの JSON 往復を実証済み。
> - `JsonWriteVisitor` / `JsonReadVisitor`（常時コンパイル）… [JsonFieldVisitor.h](../aqEngine/ECS/JsonFieldVisitor.h)。
>   Vector3=`[x,y,z]`、Quaternion=`[x,y,z,w]`（ロスレス）、FieldPath は読込時のみ true→ロード副作用発火。
> - `ImGuiFieldVisitor` を 3 引数化（`Field(persistKey, value, displayLabel=nullptr)`、2 引数呼び出しは後方互換）… [ImGuiFieldVisitor.h](../aqEngine/ECS/ImGuiFieldVisitor.h)。
> - 変換済みコンポーネント（`Inspect`→`Reflect`・`#ifdef` 外・キー/ラベル分離）: **TransformComponent**, **DecalComponent**。
> - `ComponentMeta` に `typeName` / `serialize` / `deserialize` を追加（Transform/Decal で実装）… [ComponentRegistry.h](../aqEngine/ECS/ComponentRegistry.h) / .cpp。
> - 往復テスト（Transform を JSON 化→文字列→パース→復元し一致確認）… BattleScene 初期化ブロック。Debug|x64 ビルド成功。
>
> **追加完了（Phase 2 残・対応済み）**:
> - **ComponentRegistry の常時コンパイル化**: 外側 `#ifdef AQ_DEBUG_IMGUI` を撤去し、コア
>   （typeName/serialize/deserialize/add/has/get/requiredWith）を常時コンパイル化。`drawInspector` の
>   代入のみ `#ifdef AQ_DEBUG_IMGUI` で囲む。`RegisterCoreComponents()` の呼び出しと include を
>   Application で常時化。**→ リリースビルド（AQ_DEBUG_IMGUI 無効）でも JSON からコンポーネントを
>   復元できることを Release|x64 ビルドで実証**。
> - **StaticMeshComponent** を Reflect 化（model/texture パス）。`SetModelPath` の副作用（shaderType 設定との
>   順序）を避けるため `OnDeserialized()` 後処理へ退避し、deserialize ラムダで `Reflect`→`OnDeserialized` を呼ぶ。
>   ImGui 編集はパスのコミット制御が必要なため `Inspect` を別途維持（このコンポーネントのみ Inspect/Reflect 併存）。
>
> **未完（Phase 2 残）**:
> - 直接 ImGui を呼ぶ `Inspect`（**OceanComponent** / **TerrainComponent** の HeightScale 等）は visitor 非依存のため未変換。要書き直し。
> - StaticMesh の PBR パラメータ/shaderType の serialize（現状は model/texture パスのみ）。
> - **AudioSourceComponent** 等は綺麗な visitor 駆動だが ComponentRegistry 未登録（別パネル管理）のため、レジストリ駆動シリアライズの対象外。
> - `HierarchicalTransformComponent` は親子/ワールド座標が runtime/構造由来のため **serialize 対象外**（Prefab ツリー構造が階層を表す）。

### 5.1 Reflect は常時コンパイル・visitor 非依存に書き直す

各コンポーネントの `Reflect` は「永続フィールドの列挙のみ」。ImGui 専用ロジック（コピー→Enter コミット、
PBR 条件表示、`SetModelPath` 副作用）は visitor 側 / deserialize 後処理へ退避。

### 5.2 永続キーと表示ラベルを分離

```cpp
template <typename V>
void Reflect(V& visitor)
{
    visitor.Field("position", position, "position (local)");  // (persistKey, value, displayLabel)
    visitor.Field("rotation", rotation, "rotation (local)");
    visitor.Field("scale",    scale,    "scale (local)");
}
```

| Visitor | `Field()` | キー | ビルド |
|---|---|---|---|
| `ImGuiFieldVisitor`（既存→流用） | UI 描画 | displayLabel | DEBUG |
| `JsonWriteVisitor`（新規） | `json[persistKey]=value` | persistKey | 常時 |
| `JsonReadVisitor`（新規） | `value=json[persistKey]` | persistKey | 常時 |

`ReadOnly()` は serialize visitor では no-op。

### 5.3 ComponentRegistry の拡張

```cpp
struct ComponentMeta
{
    const char* typeName;        // JSON キー（例 "Transform"）★
    // 既存: requiredWith / has / get / add / remove / drawInspector(EntityHandle)
    std::function<void(EntityHandle, util::JsonValue&)>       serialize;       // ★
    std::function<void(EntityHandle, const util::JsonValue&)> deserialize;     // ★
    // --- 型消去（Prefab エディタ用、§6）---
    std::function<void(void*)>                                construct;       // ★ placement-new（全型）
    std::function<void(void*)>                                destruct;        // ★
    std::function<void(void*, util::JsonValue&)>              serializePtr;    // ★
    std::function<void(void*, const util::JsonValue&)>        deserializePtr;  // ★
    std::function<void(void*)>                                drawInspectorPtr;// ★ void* 版
};
const ComponentMeta* Find(std::string_view typeName) const;   // ★名前引き
TypeInfo            TypeOf(std::string_view typeName) const;   // ★名前→TypeInfo
```

コア部は常時コンパイル、`drawInspector*` だけ `#ifdef AQ_DEBUG_IMGUI`。

## 6. ランタイム表現とエディタ表現（揺れの解消）

**ランタイム（Phase 3 の初期実装）と エディタ（Phase 5）で表現を明確に分ける。**

### 6.1 ランタイム = `PrefabData`（JsonValue ベースの不変プラン）

```cpp
// 解決済み・不変の生成プラン。shared_ptr で共有し、遅延コマンドが値捕獲する（§4.3）。
struct PrefabNodeData
{
    std::string                  name;
    util::JsonValue              components;   // { "Transform": {...}, ... }（解決済み・overrides 適用後）
    std::vector<PrefabNodeData>  children;     // ネスト参照は Load 時に展開済み
};
struct PrefabData { PrefabNodeData root; };    // 不変
```

- 生成は §4.3 のとおり「型リスト（components のキー→TypeInfo）→ 遅延生成 → 各コンポーネント `deserialize`(JsonValue)」。
- **初期実装は毎スポーン deserialize**。CopyFn ブループリント最適化は後段（§10）で、この JsonValue 経路の上に載せる。

> **実装状況（Phase 3 完了）**: 単一 JSON プランからツリーを遅延生成し、実機（BattleScene 初期化）で検証済み。
> - `PrefabData` / `PrefabNodeData`（JsonValue ベースの不変プラン）+ 薄いハンドル `Prefab`（`shared_ptr<const PrefabData>` 保持）… [Prefab.h](../aqEngine/ECS/Prefab.h)。
> - ツリー遅延生成（`Prefab::Instantiate`）… [Prefab.cpp](../aqEngine/ECS/Prefab.cpp)。**shared_ptr を値捕獲**（§4.3 寿命ルール）。
>   1 コマンド内で root→子→孫を再帰生成し、各ノードで `CollectTypes`（components キー→TypeInfo + **requiredWith 推移展開**）→生成→`deserialize`(JsonValue)→DebugTag 命名→HTC 親子付け。
>   末尾で `onComplete(root)`。即時版 `InstantiateImmediate`（init/エディタ用・ForEach 外限定）も提供。
> - 生成プリミティブ … [EntityManager.h](../aqEngine/ECS/EntityManager.h)：`RequestDeferredBuild`（複数 Entity を 1 コマンドで生成する遅延フック・registry 非依存）+ `CreateEntityFromTypes`（即時・ロック版）。
>   [EntityContext.h](../aqEngine/ECS/EntityContext.h) のラッパで EntityDebugTag を注入（`InjectDebugTag` に共通化）。
> - JSON 層 … [PrefabSerializer.h](../aqEngine/ECS/PrefabSerializer.h) / .cpp：`Load(path)` / `FromJson(JsonValue)`。**Phase 3 はインライン components/children のみ**（"prefab" ネスト参照・overrides・循環検出は Phase 6）。
> - レジストリ拡張 … [ComponentRegistry.h](../aqEngine/ECS/ComponentRegistry.h)：`Find(typeName)` / `TypeOf(typeName)`（名前→meta / 名前→TypeInfo）。
> - 検証テスト（root→child→grandchild の Transform-only ツリーを JSON から遅延生成し、`onComplete` で local position 復元と親子構造を assert）… BattleScene 初期化ブロック。Debug|x64 ビルド成功。
> - **旧コードベース `Prefab`（`Component/Prefab.{h,cpp}` の `Prefab::Create<Cs...>`）は削除**（新データ駆動 `aq::ecs::Prefab` へ置換）。BattleScene の旧階層テストは上記 JSON ツリーテストへ移行。
>
> **未完（Phase 3 残・Phase 6 へ）**:
> - `onComplete` は現状 `isInUserCallback_` 未設定で呼ばれる（builder 全体が構造変更可コンテキスト）。厳密化は primitive を汚さない形で後段検討。
> - "prefab" ネスト参照展開・overrides 意味論・循環検出（§7.3/§7.4）は Phase 6。

### 6.2 エディタ = 型消去コンポーネント実体（編集用ワーキングコピー）

エディタは編集 UX のため、コンポーネントを**実体**として保持し既存 Inspector を再利用する。
ランタイムの `PrefabData` とは別物で、**Save 時に JsonValue へ、Load 時に JsonValue から**相互変換する。

```cpp
// 型消去ストレージ。アライメント保証 + move-only（バイトコピー禁止）。
class AlignedStorage   // Chunk の AllocBuffer と同方式: operator new(bytes, align_val_t(align))
{
    void*    ptr_   = nullptr;
    TypeInfo type_;
public:
    explicit AlignedStorage(TypeInfo t);   // operator new(align_val_t) で確保し construct
    ~AlignedStorage();                      // destruct（destructor_ があれば）+ operator delete(align_val_t)
    AlignedStorage(AlignedStorage&&) noexcept;            // mover_ で move-construct
    AlignedStorage(const AlignedStorage&)            = delete;  // ★コピー禁止（CopyFn 未定義のため）
    AlignedStorage& operator=(const AlignedStorage&)  = delete;
    void* Get() const { return ptr_; }
};

struct PrefabComponent { AlignedStorage data; };   // type は data.type_
struct PrefabNode
{
    std::string                  name;
    std::vector<PrefabComponent> components;        // 編集対象の実体
    std::vector<PrefabNode>      children;
    std::string                  prefabRef;          // ネスト参照（空=なし）
    util::JsonValue              overrides;
};
```

- **アライメント**: `vector<uint8_t>` は使わない。`operator new(bytes, std::align_val_t(type.GetAlign()))` で確保（Chunk と同方式）。
- **コピー/破棄**: `AlignedStorage` は move-only。非 trivial コンポーネントをバイトコピーで壊さない。
  破棄時は `TypeInfo::GetDestructor()`、move 時は `TypeInfo::GetMover()` を使用。

### 6.3 エディタ UI（既存資産の再利用）

[SceneHierarchySystem.cpp:242](../aqEngine/ECS/SceneHierarchySystem.cpp#L242) の Inspector ループと同パターンを `void*`（`AlignedStorage::Get()`）に対して回す:

- 階層ツリー表示・ノード追加/削除/リネーム
- 「Add Component」= `GetAll()` 列挙 + `meta.construct`
- 編集 = `meta.drawInspectorPtr(storage)`（内部で `Reflect` + `ImGuiFieldVisitor`）
- Save = `meta.serializePtr` → JSON → `.prefab.json`
- Load = JSON → `meta.construct` + `meta.deserializePtr`
- PrefabReference は登録済み Prefab のドロップダウン（§8）

→ エディタ・シリアライザ・ランタイム生成が **`Reflect` を単一の真実**として共有。

> **実装状況（Phase 5 完了）**: ImGui で Prefab を作成・編集・保存・ロード・プレビュー生成可能。Debug/Release 両ビルド成功。
> - `AlignedStorage`（型消去・`operator new(align_val_t)` でアライン確保・move-only=ヒープポインタ移譲・破棄/構築は TypeInfo 経由）… [AlignedStorage.h](../aqEngine/ECS/AlignedStorage.h)。
>   ※ ヒープ確保のため move はポインタ移譲で十分（`GetMover()` は不要）。`construct`/`destruct` は AlignedStorage が TypeInfo で行うため meta には持たせない。
> - ComponentMeta に **型消去 void\* 版** `serializePtr` / `deserializePtr` / `drawInspectorPtr` を追加（`drawInspectorPtr` のみ `#ifdef AQ_DEBUG_IMGUI`）… [ComponentRegistry.h](../aqEngine/ECS/ComponentRegistry.h)。
>   Reflect 化済みコンポーネント（Transform/StaticMesh/Decal/PrefabReference/Spawner）へ `FillReflectPtrFns<T>` で一括設定… [ComponentRegistry.cpp](../aqEngine/ECS/ComponentRegistry.cpp)。Reflect を単一経路として共有。
> - エディタパネル `PrefabEditorPanel`（`IDebugRenderable`）… [PrefabEditor.h](../aqEngine/ECS/PrefabEditor.h) / .cpp：
>   編集ツリー（`PrefabEditNode`=name+型消去 components+children・ポインタ安定のため children は `unique_ptr`）、
>   階層ツリー UI（選択・Add Child・Delete は描画後に予約実行）、インスペクター（`drawInspectorPtr(void*)` で実体編集・Add Component パレットは drawInspectorPtr を持つ型のみ・重複除外・Remove）、
>   Save（編集ツリー→JSON→`.prefab.json`）、Load（JSON→`AlignedStorage`+`deserializePtr`）、Spawn Preview（編集ツリー→JSON→`PrefabSerializer::FromJson`→`Instantiate`）。
> - BattleScene で `DebugUI` に登録/解除… `BattleScene.cpp`（旧構成）。
>
> **未完（Phase 5 残・Phase 6 へ）**:
> - PrefabReference のドロップダウン（登録 Prefab 一覧）は未実装（現状は文字列 path を直接編集）。
> - 編集ツリーの `prefabRef` / `overrides`（ネスト参照・バリアント）は Phase 6。
> - StaticMesh はエディタ実体編集で `OnDeserialized`（メッシュロード副作用）を呼ばない（パス文字列のみ編集）。

## 7. JSON プレハブ形式とオーバーライド

### 7.1 形式（`.prefab.json`）

```json
{
  "name": "Enemy",
  "components": {
    "Transform":    { "position": [0,0,0], "rotation": [0,0,0], "scale": [1,1,1] },
    "SkeletalMesh": { "path": "Assets/enemy.fbx" }
  },
  "children": [
    {
      "name": "Sword",
      "prefab": "Sword.prefab.json",
      "overrides": {
        "components":        { "Transform": { "position": [0,1,0] } },
        "removedComponents": [ "SkeletalMesh" ]
      }
    }
  ]
}
```

### 7.2 JSON 方言

`//` 行コメントのみ。`/* */`・末尾カンマ・シングルクォート不可。

### 7.3 オーバーライド意味論

- `overrides.components` … 既存へ deep merge / `addedComponents` … 新規追加 / `removedComponents` … 除去
- children は **`name` 同定**: `overrides.children`（name 一致で再帰、無ければ追加） / `removedChildren`（name 除去）
- 同一親内の子 `name` は一意前提（loader で検証・重複は診断ログ）
- サポート範囲は add/override/remove まで（Unity の任意プロパティ単位追跡は対象外）。

### 7.4 ネスト参照の解決と循環検出

`PrefabSerializer`: baseDir 基準・正規化パスでキー化・ロードスタックで循環検出（エラー）・最大深度・
欠落/失敗は診断エラー（無言フォールバックしない）・正規化パスでキャッシュ。
**展開は Load 時に完了**し、`PrefabData` には参照を残さない（ランタイムは自己完結）。

> **実装状況（Phase 6 完了）**: ネスト参照展開・循環検出・overrides 意味論を実装し検証済み。Debug/Release 両ビルド成功。
> - `PrefabSerializer` を全面拡張… [PrefabSerializer.cpp](../aqEngine/ECS/PrefabSerializer.cpp)：
>   `ResolveNode`（"prefab" 参照を baseDir 相対で解決→再帰展開、name/overrides 適用）、
>   `LoadContext`（**ロードスタックで循環検出**・正規化パスで **parseCache**・**最大深度 32**）、
>   欠落/循環/深度超過は `EnginePrintf` で診断ログを出し空ノードを返す（クラッシュせず無言フォールバックもしない）。
>   **展開は Load/FromJson 時に完了**し、`PrefabData` には参照を残さない。
> - overrides 意味論 `ApplyPatch` / `PatchComponents`：
>   `components`=deep merge（`JsonValue::Merge`）/ `addedComponents`=新規 / `removedComponents`=除去、
>   `children`=name 同定（同名は再帰 `ApplyPatch`・無ければ新規解決して追加）/ `removedChildren`=name 除去、name 上書き対応。
> - `FromJson(root, baseDir="")` オーバーロード追加（ファイルを介さない動的生成でも baseDir 相対参照を解決可能）… [PrefabSerializer.h](../aqEngine/ECS/PrefabSerializer.h)。
> - 検証（インライン base + overrides で deep merge / addedComponents / removedComponents を Instantiate→onComplete で assert）… BattleScene 初期化ブロック。
>   ファイル参照展開は同じ ApplyPatch を共有（ファイル IO を伴うためテストは意味論をインラインで検証）。
> - **旧 API 廃止**: 旧コードベース `Prefab` は Phase 3 で削除済み。「Save as Prefab」相当はエディタの Save（Phase 5）で実現済み。

## 8. コンポーネントに Prefab 参照を持たせて生成する

### 8.1 PrefabId と PrefabRegistry（参照の正本は文字列）

```cpp
struct PrefabId { uint64_t value = 0; };   // ランタイムのキャッシュキー（0=無効）

class PrefabRegistry
{
public:
    static PrefabRegistry& Get();
    // path（または GUID 文字列）を正本に解決。多重ロードはキャッシュ。
    // uint64 キーは内部生成。【衝突検出】し、衝突時は診断エラーログ + 別キー割り当て。
    PrefabId      Resolve(std::string_view pathOrGuid);
    std::shared_ptr<const PrefabData> Find(PrefabId id) const;   // §4.3 で shared_ptr 捕獲
};
```

- **JSON / シリアライズ上の正本は path または GUID 文字列**。`uint64` ハッシュは保存しない
  （衝突・パス正規化差・アセット移動・ハッシュ実装変更で復元不能になるため）。
- `uint64 PrefabId` は **ランタイム専用のキャッシュキー**。Registry で衝突検出＋診断。

> **実装状況（Phase 4 完了）**: データ参照→ランタイム解決→System 遅延スポーンを実機（BattleScene）で検証済み。
> - `PrefabRegistry`（シングルトン）… [PrefabRegistry.h](../aqEngine/ECS/PrefabRegistry.h) / .cpp：
>   `Resolve(pathOrGuid)`（正規化=バックスラッシュ→スラッシュ・キャッシュ・未ロード時 `PrefabSerializer::Load`）、
>   `Register(key, Prefab)`（ファイルを介さない in-memory 登録・動的生成/テスト用）、
>   `Find(PrefabId)`（shared_ptr 返却・§4.3 値捕獲）、`Clear()`。
>   uint64 キーは FNV-1a で内部生成し、**衝突は線形プローブで別キー割当 + 診断エラー**。`value==0` は無効値に予約。
> - `PrefabReferenceComponent` / `SpawnerComponent`（正本=prefabPath 文字列、resolved/timer/spawned はランタイム・非 serialize）
>   + `SpawnSystem`（interval ごとに `Find`→`Prefab(data).Instantiate(htc->parentHandle)`・maxCount 制限）… [SpawnSystem.h](../aqEngine/ECS/SpawnSystem.h) / .cpp。
>   ForEach 中の遅延生成は commandMutex_ のみ取得のため安全。dt は `aq::Engine::GetDeltaTime()`。
> - ComponentRegistry に PrefabReference / Spawner を登録（serialize/deserialize/drawInspector）… [ComponentRegistry.cpp](../aqEngine/ECS/ComponentRegistry.cpp)。
> - SpawnSystem を engine core で登録（HierarcicalTransform/Animation と並ぶ常設 System）… [Core/Application.cpp](../aqEngine/Core/Application.cpp)。
> - 検証（Register/Resolve のキャッシュ一貫性・Find の有効/無効 id・Spawner エンティティ配置でランタイムスポーン起動）… BattleScene 初期化ブロック。Debug|x64 ビルド成功。
>
> **未完（Phase 4 残・後続へ）**:
> - スポーンの親付けは設計例どおり `htc->parentHandle`（spawner の親＝兄弟として生成）。弾のようにワールド直置きしたい場合の選択肢（無親 / 専用コンテナ）は要追加。
> - 大量スポーンの CopyFn ブループリント最適化（§10）は未着手（現状は毎スポーン deserialize）。

### 8.2 コンポーネント例：Spawner（Prefab 参照は自作コンポーネントが直接持つ）

> **専用の PrefabReferenceComponent は設けない（2026-07-01 決定・削除済み）**。
> 「Prefab を参照する」= `prefabPath`（文字列）を正本に持ち、実行時に
> `PrefabRegistry::Resolve(path) → Find(id) → Prefab::Instantiate` で解決するだけ。
> これは**任意の自作コンポーネント**が同じ手順で行えるため、参照専用コンポーネントは不要。
> `PrefabId`（uint64）は Registry 内部のキャッシュキーで、**利用側が直接値を知る必要はない**
> （常に path 文字列 → `Resolve` で取得する）。`SpawnerComponent` はその参照＋間欠生成の一例。

```cpp
// 任意の自作コンポーネントで Prefab を参照する最小パターン:
struct MyComponent : IComponent
{
    ecsComponent(aq::ecs::MyComponent);
    std::string prefabPath;            // ★正本（serialize される）。例 "Assets/Prefabs/Sword.prefab.json"
    PrefabId    resolved;              // ランタイム解決結果のキャッシュ（serialize しない）
    template <typename V> void Reflect(V& v) { v.FieldPath("prefab", prefabPath, "Prefab"); }
};
// 使う側（System 等）:
//   if (!resolved.IsValid()) resolved = PrefabRegistry::Get().Resolve(prefabPath);
//   if (auto data = PrefabRegistry::Get().Find(resolved)) Prefab(data).Instantiate(parent);

struct SpawnerComponent : IComponent
{
    ecsComponent(...);
    std::string prefabPath;            // 正本
    PrefabId    resolved;              // ランタイム
    float       interval = 1.0f;
    float       timer    = 0.0f;
    int         maxCount = -1;
    template <typename V> void Reflect(V& v)
    {
        v.FieldPath("prefab", prefabPath, "Prefab");
        v.Field("interval", interval);
        v.Field("maxCount", maxCount);
    }
};
```

```cpp
// System（Update 内）。遅延生成なので ForEach 中でも安全。
void SpawnSystem::Update()
{
    Foreach<SpawnerComponent, HierarchicalTransformComponent>(
        [&]()
    {
        if (s->resolved.value == 0) s->resolved = PrefabRegistry::Get().Resolve(s->prefabPath);  // 初回解決
        s->timer += dt;
        if (s->timer < s->interval) return;
        s->timer = 0.0f;
        // Find で shared_ptr を取得 → Instantiate がそれを値捕獲（§4.3 寿命ルール）
        if (auto data = PrefabRegistry::Get().Find(s->resolved))
            Prefab(data).Instantiate(htc->parentHandle);
    });
}
```

「データ（path 文字列）→ ランタイム解決（PrefabId）→ System が遅延生成」という UE/Unity 的運用が成立する。

## 9. 段階導入プラン（改訂4）

> **全 Phase 完了（Phase 1〜6）**。各 Phase の実装詳細は §4〜§7 のインライン「実装状況」ブロックを参照。

| Phase | 内容 | 成果 / 検証 | 状況 |
|---|---|---|---|
| **1（核心）** | `TypeInfo::ConstructFn`（**全型**）+ `RequestCreateEntityFromTypes`（遅延・requiredWith 補完・**dedup**・**MAX 超過診断エラー**・DebugTag 注入）+ 単体テスト | ForEach 内でも安全に完全な Entity を遅延生成 | ✅ 完了 |
| **2** | `Reflect` 化（per-component 書き直し・キー/ラベル分離）+ Json visitor + レジストリ serialize/deserialize | 単一エンティティの JSON 保存・復元 | ✅ 完了 |
| **3** | `ECS/Prefab`+`PrefabData`（JsonValue プラン）+ `PrefabSerializer::Load` + ツリー遅延生成（§4.3・**shared_ptr 寿命ルール**） | 単一 JSON からツリーを遅延生成 | ✅ 完了 |
| **4** | `PrefabRegistry`（文字列正本・衝突検出）+ `PrefabReference`/`Spawner` + `SpawnSystem` | データ参照からの動的スポーン | ✅ 完了 |
| **5** | Prefab エディタ（`AlignedStorage` 型消去 + drawInspectorPtr + パレット再利用） | ImGui で Prefab 作成・保存 | ✅ 完了 |
| **6** | `prefab` 参照展開 + 循環検出 + overrides 意味論 + 「Save as Prefab」→ 旧 API 廃止 | ネスト・バリアント・運用 | ✅ 完了 |

### 残課題（将来の最適化・拡張）
- **大量スポーン性能**: `TypeInfo` に `CopyFn` 追加 → ロード時に構築済みブループリントをキャッシュ→生成時コピー（§10）。現状は毎スポーン deserialize。
- **PrefabReference ドロップダウン**: エディタで登録 Prefab 一覧から選ぶ UI（現状は文字列 path 直接編集）。
- **スポーン親付けの選択肢**: 現状は spawner の親へ（兄弟生成）。ワールド直置き/専用コンテナの選択肢。
- **`onComplete` の `isInUserCallback_` 厳密化**（§4.3）。

## 10. リスク・留意点

- **Reflect 書き直しコスト**: ImGui 専用ロジック退避が各コンポーネントで発生（Phase 2 の主工数）。
- **大量スポーン性能**: 初期は毎スポーン JsonValue `deserialize`。多発する弾等は、ロード時に
  **構築済みブループリントをキャッシュ→生成時コピー**で最適化（`TypeInfo` に `CopyFn` 追加が必要）。
  この最適化は §6.1 の JsonValue 経路の上に載せる（表現は変えない）。
- **遅延の可視性**: `Instantiate` は同期的に Entity を返さない。即時ハンドルは `InstantiateImmediate`（ForEach 外）か `onComplete`。
- **型安全性**: コンパイル時 static_assert → `requiredWith` 実行時検証 + 診断ログ。
- **PrefabId 復元性**: 保存は文字列正本のみ。uint64 は実行時キャッシュキー、Registry で衝突検出。
- **レジストリ常時コンパイル化**: コア部の `#ifdef` 切り出し。


---

# 11. 使い方（Usage / クイックスタート）

Prefab = **JSON（`.prefab.json`）で定義したエンティティツリーの設計図**。作って、ランタイムで生成する。

## 11.1 Prefab を作る

### (A) ImGui エディタで作る（推奨）
デバッグメニュー **Tools > Prefab Editor** を開く（全シーンで利用可）。
1. ノードを選び「+ Add Component」で構成を組む（Reflect 対応コンポーネントのみ選択可）
2. 「+ Add Child」で子ノードを足してツリーを作る
3. フィールドを編集し、パス欄に `.prefab.json` を入れて **Save**
4. 「Spawn Preview」で現在の編集ツリーをその場に遅延生成して確認できる

### (B) `.prefab.json` を手書きする
```json
{
  "name": "Enemy",
  "components": {
    "Transform":    { "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1] },
    "SkeletalMesh": { "model": "Assets/models/enemy.tkm", "texture": "Assets/models/enemy.png" }
  },
  "children": [
    {
      "name": "Weapon",
      "prefab": "Sword.prefab.json",                 // ネスト参照（baseDir からの相対）
      "overrides": {
        "components": { "Transform": { "position": [0,1,0] } }
      }
    }
  ]
}
```
- コメントは `//` 行のみ可（`/* */`・末尾カンマ不可）。
- コンポーネントのキー = 各 `ComponentMeta::typeName`、フィールドキー = `Reflect` の persistKey（下表）。

| コンポーネント | JSON キー | 主なフィールド（persistKey） |
|---|---|---|
| Transform | `Transform` | `position` `[x,y,z]` / `rotation` `[x,y,z,w]` / `scale` `[x,y,z]` |
| Static Mesh | `StaticMesh` | `model` / `texture`（パス文字列） |
| Skeletal Mesh | `SkeletalMesh` | `model` / `texture` |
| Decal | `Decal` | `texture` / `size` / `color` / `opacity` / `angleFadeMin` |
| Spawner | `Spawner` | `prefab` / `interval` / `maxCount` |

## 11.2 Prefab を生成する（コードから）

```cpp
#include "ECS/PrefabSerializer.h"
#include "ECS/PrefabRegistry.h"

// --- 方法1: ファイルから直接ロードして生成 ---
aq::ecs::Prefab prefab = aq::ecs::PrefabSerializer::Load("Assets/Prefabs/Enemy.prefab.json");
prefab.Instantiate();                          // 遅延生成（次の ECS Update で実体化）

// --- 方法2: Registry 経由（同じ Prefab を何度も生成する場合はこちら） ---
auto& reg = aq::ecs::PrefabRegistry::Get();
aq::ecs::PrefabId id = reg.Resolve("Assets/Prefabs/Enemy.prefab.json");   // path→ID（内部で Load+キャッシュ）
if (auto data = reg.Find(id))
    aq::ecs::Prefab(data).Instantiate(parentHandle);
```

- **既定は遅延生成**：`Instantiate()` はコマンドを積むだけで、実体化は次の `EntityContext::Update()`（FlushCommands）。
  System の `Update`（ForEach）内から呼んでも安全。
- **生成完了で何かしたい**：`Instantiate(parent, [](Entity root){ /* 生成直後 */ })`。
- **その場で Entity ハンドルが欲しい**（初期化・エディタ用、ForEach 外限定）：
  `Entity e = prefab.InstantiateImmediate(parent);`

## 11.3 Component に Prefab の ID（参照）を持たせて使う

**専用の参照コンポーネントは無い。** 自作コンポーネントが `prefabPath`（文字列＝正本）を持ち、
実行時に Registry で解決するだけ。`PrefabId`（uint64）は Registry 内部のキャッシュキーなので
**利用側が値を直接知る必要はない**（常に path → `Resolve`）。

```cpp
// 1) 参照を持つコンポーネント
struct MyComponent : aq::ecs::IComponent
{
    ecsComponent(aq::ecs::MyComponent);
    std::string     prefabPath;   // ★正本（serialize される）。例 "Assets/Prefabs/Sword.prefab.json"
    aq::ecs::PrefabId resolved;   // 解決結果のキャッシュ（serialize しない）

    template <typename V>
    void Reflect(V& v) { v.FieldPath("prefab", prefabPath, "Prefab"); }   // エディタ/JSON で path 編集
};

// 2) 使う側（System の Update 内など）
if (!comp->resolved.IsValid() && !comp->prefabPath.empty())
    comp->resolved = aq::ecs::PrefabRegistry::Get().Resolve(comp->prefabPath);   // 初回だけ解決
if (auto data = aq::ecs::PrefabRegistry::Get().Find(comp->resolved))
    aq::ecs::Prefab(data).Instantiate(parentHandle);                             // 遅延生成
```

- エディタ/シリアライズに載せるには、この `MyComponent` を `ComponentRegistry::RegisterCoreComponents()` に
  他コンポーネント同様に登録し、`FillReflectPtrFns<MyComponent>(meta)` を呼ぶ（`typeName` と serialize/deserialize が付く）。
- **既成の実例**：`SpawnerComponent`（`prefabPath` + `interval` で間欠スポーン）と `SpawnSystem`
  （[SpawnSystem.cpp](../aqEngine/ECS/SpawnSystem.cpp)）。`SpawnSystem` は `AddSystem` 済みなので、エンティティに
  `Spawner` コンポーネントを付けて `prefab` に path を入れるだけで自動スポーンが動く。

### ファイルを介さない動的 Prefab
コードで組んだ Prefab を ID 参照したいときは Registry に文字列キーで登録する：
```cpp
aq::ecs::Prefab p = aq::ecs::PrefabSerializer::FromJson(jsonValue);   // JsonValue から組む
aq::ecs::PrefabId id = aq::ecs::PrefabRegistry::Get().Register("mem://bullet", p);
// 以降は Resolve("mem://bullet") で取得できる
```

## 11.4 注意点
- `Instantiate` は同期的に Entity を返さない（遅延）。ハンドルが要るなら `InstantiateImmediate`（ForEach 外）か `onComplete`。
- Prefab の寿命：`Instantiate` は内部の `shared_ptr<const PrefabData>` を値捕獲するので、一時 Prefab や
  Registry の `Clear()`／リロード後でも安全（use-after-free しない）。
- 保存されるのは path 文字列。`PrefabId`(uint64) は保存しない（アセット移動・ハッシュ変更で壊れるため）。
