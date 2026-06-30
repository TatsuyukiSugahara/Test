# Prefab データ駆動化 設計（改訂4）

> 改訂3 → 改訂4 の変更（再レビュー反映）:
> - **High**: `ConstructFn` は trivial 型でも nullptr にせず**全型で必須**（Chunk は生メモリ、構築省略は未初期化バグ）。
> - **High**: 遅延 `Instantiate` コマンドの**寿命ルールを確定**（`this`/参照を捕まえない。`shared_ptr<const PrefabData>` を捕獲）。
> - **Medium**: 型消去ストレージの**アライメント保証**と **move-only / CopyFn** を明記。
> - **Medium**: ランタイム表現を**「JsonValue ベースのプラン」に固定**（型消去実体はエディタ専用）。揺れを解消。
> - **Medium**: `PrefabId` は **JSON 上は path/GUID 文字列が正本**、uint64 はランタイムキャッシュキーのみ。衝突検出＋診断。
> - **Low**: requiredWith 展開後の **dedup と MAX_COMPONENT_SIZE 超過の診断エラー化**を Phase 1 に明記。

## 1. 目的と背景

[../Component/Prefab.h](../Component/Prefab.h) の `Prefab` はコード版スポナー（構成がコンパイル時固定・初期値ラムダ・保存/読込/参照なし）。
これを **データ（JSON）駆動の設計図**へ作り直す。主要ユースケースは **ゲーム実行中の動的生成**（弾・敵・エフェクトを System から spawn）。

## 2. 既存資産と確認済みの制約

| 資産 | 場所 | 備考 |
|---|---|---|
| 中央レジストリ（TypeInfo→meta） | [ComponentRegistry.h](ComponentRegistry.h) | `vector<pair<TypeInfo, meta>>`。名前→TypeInfo 解決の土台 |
| 既存 Inspector / Add パレット | [SceneHierarchySystem.cpp:242](SceneHierarchySystem.cpp#L242) | `GetAll()`→`meta.has/add/drawInspector(handle)`。エディタで再利用 |
| 実行時 Archetype 構築 | [Archetype.h:97](Archetype.h#L97) `AddType(const TypeInfo&)` | **重複を弾かない・上限超過は無言無視**（→ dedup/診断は呼び出し側責務） |
| 遅延 Entity 生成 | [EntityManager.h:77](EntityManager.h#L77) `RequestCreateEntity` | コマンド実行時に「生成→**全構築 `new(...) Ts()`**→onCreated」。onCreated 内で GetComponent 有効 |
| コンポーネント構築 | [EntityManager.h:320](EntityManager.h#L320) `NewComponent` | **triviality 無関係に常に placement-new**。Chunk は生メモリ |
| アライン確保 | [Chunk.h:164](Chunk.h#L164) `AllocBuffer` | `operator new(bytes, align_val_t(align))`。型消去ストレージも同方式が必要 |
| JSON | [../Util/SimpleJson.h](../Util/SimpleJson.h) | パーサ/シリアライザ + `Merge`。コメントは `//` 行のみ |
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
> - `TypeInfo`: `ConstructFn`（全型必須）+ `Construct()` … [TypeInfo.h](TypeInfo.h)
> - `Chunk::GetComponentByType(loc, type)` … [Chunk.h](Chunk.h)
> - `EntityManager::RequestCreateEntityFromTypes` / `CreateEntityFromTypesNoLock` / `DedupTypes` … [EntityManager.h](EntityManager.h)
> - `EntityContext::RequestCreateEntityFromTypes`（DebugTag 注入）… [EntityContext.h](EntityContext.h)
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
> - `JsonWriteVisitor` / `JsonReadVisitor`（常時コンパイル）… [JsonFieldVisitor.h](JsonFieldVisitor.h)。
>   Vector3=`[x,y,z]`、Quaternion=`[x,y,z,w]`（ロスレス）、FieldPath は読込時のみ true→ロード副作用発火。
> - `ImGuiFieldVisitor` を 3 引数化（`Field(persistKey, value, displayLabel=nullptr)`、2 引数呼び出しは後方互換）… [ImGuiFieldVisitor.h](ImGuiFieldVisitor.h)。
> - 変換済みコンポーネント（`Inspect`→`Reflect`・`#ifdef` 外・キー/ラベル分離）: **TransformComponent**, **DecalComponent**。
> - `ComponentMeta` に `typeName` / `serialize` / `deserialize` を追加（Transform/Decal で実装）… [ComponentRegistry.h](ComponentRegistry.h) / .cpp。
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

[SceneHierarchySystem.cpp:242](SceneHierarchySystem.cpp#L242) の Inspector ループと同パターンを `void*`（`AlignedStorage::Get()`）に対して回す:

- 階層ツリー表示・ノード追加/削除/リネーム
- 「Add Component」= `GetAll()` 列挙 + `meta.construct`
- 編集 = `meta.drawInspectorPtr(storage)`（内部で `Reflect` + `ImGuiFieldVisitor`）
- Save = `meta.serializePtr` → JSON → `.prefab.json`
- Load = JSON → `meta.construct` + `meta.deserializePtr`
- PrefabReference は登録済み Prefab のドロップダウン（§8）

→ エディタ・シリアライザ・ランタイム生成が **`Reflect` を単一の真実**として共有。

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

### 8.2 コンポーネント例：PrefabReference / Spawner

```cpp
struct PrefabReferenceComponent : IComponent
{
    ecsComponent(...);
    std::string prefabPath;            // ★正本（serialize される）。例 "Assets/Prefabs/Sword.prefab.json"
    PrefabId    resolved;              // ランタイム解決結果（serialize しない）
    template <typename V> void Reflect(V& v) { v.FieldPath("prefab", prefabPath, "Prefab"); }
};

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
        [&](Entity e, SpawnerComponent* s, HierarchicalTransformComponent* htc)
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

| Phase | 内容 | 成果 / 検証 |
|---|---|---|
| **1（核心）** | `TypeInfo::ConstructFn`（**全型**）+ `RequestCreateEntityFromTypes`（遅延・requiredWith 補完・**dedup**・**MAX 超過診断エラー**・DebugTag 注入）+ 単体テスト | ForEach 内でも安全に完全な Entity を遅延生成 |
| **2** | `Reflect` 化（per-component 書き直し・キー/ラベル分離）+ Json visitor + レジストリ serialize/deserialize | 単一エンティティの JSON 保存・復元 |
| **3** | `ECS/Prefab`+`PrefabData`（JsonValue プラン）+ `PrefabSerializer::Load` + ツリー遅延生成（§4.3・**shared_ptr 寿命ルール**） | 単一 JSON からツリーを遅延生成 |
| **4** | `PrefabRegistry`（文字列正本・衝突検出）+ `PrefabReference`/`Spawner` + `SpawnSystem` | データ参照からの動的スポーン |
| **5** | Prefab エディタ（`AlignedStorage` 型消去 + drawInspectorPtr + パレット再利用） | ImGui で Prefab 作成・保存 |
| **6** | `prefab` 参照展開 + 循環検出 + overrides 意味論 + 「Save as Prefab」→ 旧 API 廃止 | ネスト・バリアント・運用 |

## 10. リスク・留意点

- **Reflect 書き直しコスト**: ImGui 専用ロジック退避が各コンポーネントで発生（Phase 2 の主工数）。
- **大量スポーン性能**: 初期は毎スポーン JsonValue `deserialize`。多発する弾等は、ロード時に
  **構築済みブループリントをキャッシュ→生成時コピー**で最適化（`TypeInfo` に `CopyFn` 追加が必要）。
  この最適化は §6.1 の JsonValue 経路の上に載せる（表現は変えない）。
- **遅延の可視性**: `Instantiate` は同期的に Entity を返さない。即時ハンドルは `InstantiateImmediate`（ForEach 外）か `onComplete`。
- **型安全性**: コンパイル時 static_assert → `requiredWith` 実行時検証 + 診断ログ。
- **PrefabId 復元性**: 保存は文字列正本のみ。uint64 は実行時キャッシュキー、Registry で衝突検出。
- **レジストリ常時コンパイル化**: コア部の `#ifdef` 切り出し。
