# Level システム 設計（初版）

> Unity/UE の Scene/Level 概念を導入する。データ駆動（JSON）で Entity ツリーを束ね、
> ロード/アンロード・サブLevel・ストリーミング（World Partition 相当）を提供する。
> Prefab データ駆動化（[../ECS/Prefab設計.md](../ECS/Prefab設計.md)）の資産を最大限再利用する。

## 0. 決定事項

- **命名**: 新規サブシステムは **Level** に統一する。既存 `app::IScene` / `app::SceneManager`
  （[../../Game/Application/Scene/Scene.h](../../Game/Application/Scene/Scene.h)）は **破棄予定**（§12 移行計画）。
- **配置**: `aqEngine/Level/`（本ディレクトリ）。Prefab（`aqEngine/ECS/`）とは分離するが密に連携する。
- **ワールド方式**: 全 Level は **単一グローバル `EntityContext` のワールドを共有**し、各 Entity に
  所属 Level を示すタグコンポーネント（`LevelMemberComponent`）を持たせる（Unity のシーンモデル）。
  Level ごとの分離ワールドは採用しない（全 System / 描画 / 物理 / HTC 階層がワールド横断前提のため）。

## 1. 現状資産と再利用方針

| 既存資産 | 場所 | Level での役割 |
|---|---|---|
| 単一グローバル ECS | [../ECS/EntityContext.h](../ECS/EntityContext.h) | Level は**このワールドを共有** |
| Prefab 不変プラン + 薄いハンドル | [../ECS/Prefab.h](../ECS/Prefab.h) `PrefabData`/`Prefab` | **Level のエンティティ = Prefab フォレスト**として流用 |
| ツリー遅延生成 | `Prefab::Instantiate` / `EntityManager::RequestDeferredBuild` | Level ロードの生成経路に流用（`onEachCreated` フック1本追加） |
| 参照解決/override 展開 | [../ECS/PrefabSerializer.h](../ECS/PrefabSerializer.h) `ResolveNode` | Level の entity ノード解決に流用 |
| path→データ 解決/キャッシュ | [../ECS/PrefabRegistry.h](../ECS/PrefabRegistry.h) | `LevelRegistry` のひな型 |
| タグ型の自動注入 | [../ECS/EntityContext.h:229](../ECS/EntityContext.h#L229) `InjectDebugTag` | `LevelMemberComponent` 注入に同型を採用 |
| データ参照→System 遅延生成 | [../ECS/SpawnSystem.h](../ECS/SpawnSystem.h) `Spawner`/`SpawnSystem` | `LevelStreamComponent`/`LevelStreamSystem` のひな型 |
| AABB | [../Math/Bounds.h](../Math/Bounds.h) `math::AABB` | World Partition のセル境界 |

> 新規に本当に必要なコアは **①LevelId 採番 + Level ツリー管理 + Unload 走査**、
> **②グリッド定義 + ストリーミングソース + 距離判定** の2点のみ。生成/破棄の実体は既存 ECS/Prefab を使う。

## 2. 用語と全体像

```
app::(GameState)  … 旧 SceneManager を置換するゲーム状態層（どの root Level を読むか決める）
   └─ aq::level::LevelManager      … ロード済み Level のツリーを所有・Load/Unload の実体
        ├─ Main   (LevelId#1, root, loadOnStart)
        │    ├─ Town    (LevelId#2, subLevel, loadOnStart)
        │    └─ Dungeon (LevelId#3, subLevel, 休眠→手動/自動ストリーム)
        └─ ...
   └─ aq::level::WorldPartition    … 巨大ワールドをセル分割し距離で自動 Load/Unload（§9）

aq::ecs::EntityContext (単一ワールド)  … 各 Entity は LevelMemberComponent{ LevelId } を保持
```

- **Level**: Entity フォレスト + サブLevel参照 + メタデータ を束ねたデータ資産（`.level.json`）。
- **サブLevel**: Level の中の Level。`loadOnStart` フラグで「最初から読む / 後から自分で読む」を制御。
- **World Partition**: Level の上に載る「セル=Level を距離で自動ストリームする」ドライバ（オープンワールド用）。

## 3. ファイル構成（`aqEngine/Level/`）

| ファイル | 内容 | Phase |
|---|---|---|
| `LevelId.h` | `LevelId`（世代付きハンドル）+ 無効値 | L1 |
| `LevelComponents.h` | `LevelMemberComponent` / `LevelStreamComponent` / `StreamingSourceComponent` | L1/L6/W2 |
| `LevelData.h` | `LevelData` / `SubLevelRef`（不変プラン） | L2 |
| `LevelSerializer.{h,cpp}` | `.level.json` ⇄ `LevelData`（entity ノードは PrefabSerializer に委譲） | L2 |
| `LevelRegistry.{h,cpp}` | path→`LevelData` 解決・キャッシュ | L2 |
| `LevelManager.{h,cpp}` | ロード済み Level ツリー・Load/Unload・startup | L2〜L5 |
| `LevelStreamSystem.{h,cpp}` | `LevelStreamComponent` 駆動の動的 Load/Unload | L6 |
| `WorldPartition.{h,cpp}` | `.world.json`・グリッド・セル管理 | W1 |
| `WorldStreamingSystem.{h,cpp}` | ストリーミングソース集計→WorldPartition 更新 | W2 |

> 追加ファイルは `aqEngine/Engine.vcxproj` の `<ClInclude>` / `<ClCompile>` に登録する
> （[SpawnSystem の登録例](../Engine.vcxproj) と同様）。

---

## 4. データ形式 `.level.json`

Level の entity エントリは実質 **Prefab ノードのフォレスト**。`prefab` 参照 / `overrides` は
Prefab Phase 6 で完成済みの経路をそのまま使う。JSON 方言も既存踏襲（`//` 行コメントのみ）。

```json
{
  "name": "Main",
  "entities": [
    { "name": "Player", "prefab": "Assets/Prefabs/Player.prefab.json",
      "overrides": { "components": { "Transform": { "position": [0,0,0] } } } },
    { "name": "Ground", "components": { "Transform": { }, "StaticMesh": { } } }
  ],
  "subLevels": [
    { "level": "Town.level.json",    "loadOnStart": true  },
    { "level": "Dungeon.level.json", "loadOnStart": false }   // 休眠（後からストリーム）
  ]
}
```

- `entities` … トップレベル Prefab ツリーの配列（インライン or ファイル参照）。
- `subLevels[].loadOnStart` … **「最初から読み込む / 途中から自分で読む」フラグ**（要望）。

## 5. ランタイム型

```cpp
namespace aq
{
    namespace level
    {
        static constexpr uint32_t INVALID_LEVEL_INDEX = 0xffffffff;

        // ecs::EntityHandle と同じ世代付きハンドル。破棄済み Level への stale unload を防ぐ。
        struct LevelId
        {
            uint32_t index      = INVALID_LEVEL_INDEX;
            uint32_t generation = 0;

            bool IsValid() const { return index != INVALID_LEVEL_INDEX; }
            bool operator==(const LevelId& other) const
            {
                return index == other.index && generation == other.generation;
            }
        };

        // 別 .level.json への参照 + ロードタイミング。
        struct SubLevelRef
        {
            std::string path;
            bool        loadOnStart = true;
        };

        // 解決済み・不変の Level プラン（ecs::PrefabData と同じ流儀）。
        // entities は overrides 適用後・ネスト参照展開後の最終形。
        struct LevelData
        {
            std::string                      name;
            std::vector<ecs::PrefabNodeData> entities;   // Prefab フォレストを再利用
            std::vector<SubLevelRef>         subLevels;
        };
    }
}
```

`LevelMemberComponent` はランタイム所有情報なので **serialize しない**
（HTC の親子ハンドルが serialize 対象外なのと同じ扱い）。Load 時にアーキタイプへ含め、
生成直後に `levelId` を差すだけにするため「型」として持たせる。

```cpp
namespace aq
{
    namespace level
    {
        // Level 所属を示すタグ。Load 時に全 Entity へ付与し、Unload の走査対象になる。
        struct LevelMemberComponent : public ecs::IComponent
        {
            ecsComponent(aq::level::LevelMemberComponent);
            LevelId levelId;
            template <typename V> void Reflect(V&) {}   // 永続化対象なし
        };
    }
}
```

> **実装状況（L1 完了）**: 基盤型と Prefab 側フックを実装し Debug|x64 ビルド成功。
> - `LevelId`（世代付きハンドル）… [LevelId.h](LevelId.h)。
> - `LevelMemberComponent`（タグ・serialize なし）… [LevelComponents.h](LevelComponents.h)。
> - Prefab 木構築の再利用口を追加 … [../ECS/Prefab.h](../ECS/Prefab.h) / [../ECS/Prefab.cpp](../ECS/Prefab.cpp)：
>   低レベル API `InstantiatePrefabTree(root, parent, create, onEachCreated)` を公開し、
>   `InstantiateNode` に `onEachCreated`（各ノード生成・deserialize 直後に発火）を追加。
>   `Prefab::Instantiate` / `InstantiateImmediate` は `onEachCreated=nullptr` で呼ぶ薄いラッパに変更（挙動不変）。
>   → L2 の Level ロードは「`LevelMemberComponent` 型を注入する `create`」+「`levelId` を差す `onEachCreated`」で
>   `InstantiatePrefabTree` を呼ぶだけでよい。

## 6. LevelRegistry / LevelManager

```cpp
// PrefabRegistry と対。JSON→LevelData の解決とキャッシュ。
// entity ノードの "prefab"/overrides 展開は PrefabSerializer に委譲する。
class LevelRegistry
{
public:
    static LevelRegistry& Get();
    std::shared_ptr<const LevelData> Load(std::string_view pathOrId);  // 正規化パスでキャッシュ
    void Clear();
};


class LevelManager
{
    // ── ロード済み Level スロット（generation 管理・freelist・親子ツリー）──

public:
    static LevelManager& Get();

    // --- プログラムからの初期 Level 指定 ---
    void    SetStartupLevel(std::string_view pathOrId);
    void    LoadStartup();                        // startup + loadOnStart サブLevel を読む

    // --- ロード / アンロード（すべて ECS コマンド経路＝遅延・System 内から安全） ---
    LevelId Load(std::string_view pathOrId, LevelId parent = LevelId());
    void    Unload(LevelId id);                   // サブLevel も再帰的に破棄
    void    UnloadAll();

    // --- 問い合わせ ---
    bool    IsLoaded(LevelId id) const;
    LevelId Find(std::string_view pathOrId) const;
};
```

> **実装状況（L2 完了・ビルド検証）**: 単一 `.level.json` の entities を遅延生成する経路を実装。Debug|x64 ビルド成功。
> - `LevelData` / `SubLevelRef` … [LevelData.h](LevelData.h)。entities は `ecs::PrefabNodeData` フォレスト。
> - `LevelSerializer`（`.level.json` → `LevelData`）… [LevelSerializer.h](LevelSerializer.h) / [.cpp](LevelSerializer.cpp)：
>   entity ノードは `ecs::PrefabSerializer::FromJson` に委譲（"prefab" 参照展開・overrides・循環検出を再利用）。
>   subLevels は保持のみ（再帰ロードは L4）。
> - `LevelRegistry`（正規化パス → `LevelData` キャッシュ + `Register`）… [LevelRegistry.h](LevelRegistry.h) / [.cpp](LevelRegistry.cpp)。
> - `LevelManager`（LevelId slot 採番・freelist・親子 children・`Load`/`IsLoaded`/`Find`）… [LevelManager.h](LevelManager.h) / [.cpp](LevelManager.cpp)：
>   `Load` は `RequestDeferredBuild` 内で「`LevelMemberComponent` 型を注入する create」+「`levelId` を差す onEachCreated」で
>   各 entity に `ecs::InstantiatePrefabTree` を呼ぶ（L1 の再利用口）。shared_ptr と LevelId を値捕獲。
>
> **未完（L2 残）**: ランタイム自己テスト（実機 or シーンで `.level.json` をロードし LevelId 付与・親子構造を assert）は未。

> **実装状況（L3 完了・ビルド検証）**: Unload を実装。Debug|x64 ビルド成功。… [LevelManager.cpp](LevelManager.cpp)
> - `Unload(id)`: `CollectSubtree`（children 再帰 DFS）で対象+子孫の LevelId を集め、`Foreach<LevelMemberComponent>` で
>   `levelId` 一致 Entity を `entity.Destroy()`（遅延破棄）。その後 `DetachFromParent` + `FreeSlot`（`data.reset` +
>   `++generation` で stale 化 + freelist 返却）。member->levelId の値を Foreach 内で読むので stale 化前後で判定は不変。
> - `UnloadAll()`: root（親なし）ロード済み Level を収集してから各 `Unload`（Unload 中の slots_ 変更に耐えるため収集/実行分離）。
> - サブLevel の再帰ロードは L4 だが、Unload のカスケード（children 走査）は L3 で実装済み（L4 で children が埋まれば自動でカスケード）。

> **実装状況（L4 完了・ビルド検証）**: サブLevel の再帰ロードを実装。Debug|x64 ビルド成功。
> - `LevelManager::Load` 末尾で `data->subLevels` のうち `loadOnStart==true` を親=このLevel で再帰 `Load`
>   （`false` は休眠＝後から手動/自動ストリーム）… [LevelManager.cpp](LevelManager.cpp)。
> - サブLevel パスは `LevelSerializer` で親 `.level.json` の baseDir 基準に `JoinPath` 解決済み（再帰ロードで直接使える）。
> - 循環サブLevel 参照（A→B→A）は `loadStack_`（正規化パス）で検出しエラーログ + 中止（無限ロード防止）。
> - Unload カスケードは L3 実装済みのため、親を Unload すれば loadOnStart サブも一括破棄される。

> **実装状況（L5 完了・ビルド検証）**: 起動 Level 指定 + ゲーム状態層への配線。ソリューション（Engine+Game）Debug|x64 ビルド成功。
> - `LevelManager::SetStartupLevel` / `LoadStartup`（`startupPath_` 保持 → `Load`）… [LevelManager.h](LevelManager.h) / [.cpp](LevelManager.cpp)。
> - サンプル起動 Level（Transform のみ・依存アセットなし）… `Game/Assets/Levels/Main.level.json`。
> - 配線: `BattleScene::Initialize()` 冒頭で `SetStartupLevel`→`LoadStartup`（`IsLoaded` を assert）、
>   `BattleScene::Finalize()` で `UnloadAll()`… [BattleScene.cpp](../../Game/Application/Scene/BattleScene.cpp)。
>   ※ 旧 `app::SceneManager`/`IScene` は温存したまま Level を併存させた段階（§12 の破棄は実運用が Level に移ってから）。
>
> **L5 実機確認済み**: 起動時に `Main.level.json` の `LevelRoot`/`Marker` が生成されることを確認。

> **実装状況（L6 完了・ビルド検証）**: コンポーネント駆動の動的ストリームを実装。ソリューション Debug|x64 ビルド成功。
> - `LevelStreamComponent`（`levelPath` 正本 + `loaded` ランタイム + `loadWhenActive`・`Reflect`）… [LevelComponents.h](LevelComponents.h)。
> - `LevelStreamSystem`（`loadWhenActive` の状態に応じ `Load`/`Unload`。Load/Unload は内部で別 Foreach を回すため、
>   走査で対象 Entity を収集してから Foreach 外で実行＝ネスト走査回避）… [LevelStreamSystem.h](LevelStreamSystem.h) / [.cpp](LevelStreamSystem.cpp)。
> - `RegisterLevelComponents()`（ECS→Level 逆依存を避け Level 層から登録。SpawnerComponent と同じ typeName/serialize/deserialize/Inspector。
>   型消去エディタ対応=FillReflectPtrFns は L7）… [LevelComponentRegistry.h](LevelComponentRegistry.h) / [.cpp](LevelComponentRegistry.cpp)。
> - 配線: `Application::Initialize` で `RegisterLevelComponents()`、`Application::Register` で `AddSystem<LevelStreamSystem>()`
>   … [Core/Application.cpp](../Core/Application.cpp)。
>
> **未完（L6 残）**: 実機での動的ストリーム確認（`loadWhenActive` トグルで Load/Unload される）は未。距離ベースの自動トリガーは World Partition（§10）。

## 7. Load / Unload フロー

### Load（遅延・LevelId stamping）
1. `LevelRegistry::Load` で `LevelData` を解決（`prefab` 参照・overrides は既存経路）。
2. `LevelManager` が **LevelId を採番**し、`parent` の下にツリー登録。
3. `EntityManager::RequestDeferredBuild` で 1 コマンド内に `entities` フォレストを再帰生成。
   - 生成する型列へ **`LevelMemberComponent` を注入**（`InjectDebugTag` と同型のフック）。
   - 各 Entity 生成直後に `member->levelId = thisLevelId` を差す
     （**Prefab 生成に `onEachCreated` フックを1本追加**するのが最小改修）。
4. `subLevels` のうち `loadOnStart==true` を `Load(subPath, thisLevelId)` で再帰ロード。
   `false` は「登録済みだが休眠」＝後からストリーム可能。

### Unload（遅延）
1. 対象 LevelId と全子孫を「unloading」にし、完了時に generation を進める（stale 化）。
2. `Foreach<LevelMemberComponent>` で `levelId` 一致（子孫含む）を `RequestDestroyEntity`。
3. マネージャのツリーから除去。

> **寿命ルール（Prefab設計 §4.3 踏襲）**: Load/Unload の遅延コマンドは `this` 参照を捕獲せず、
> `std::shared_ptr<const LevelData>` と `LevelId`（値）だけを捕獲する。

## 8. コンポーネント駆動の動的ロード

「Component に Level の ID を持たせて動的に」を、`Spawner`/`SpawnSystem` と同じ構図で実現する。

```cpp
// トリガーボリューム等に付け、条件成立でサブLevel をストリーム。
struct LevelStreamComponent : public ecs::IComponent
{
    ecsComponent(aq::level::LevelStreamComponent);
    std::string levelPath;          // 正本（serialize）
    LevelId     loaded;             // ランタイム解決結果（serialize しない）
    bool        loadWhenActive = true;

    template <typename V> void Reflect(V& v)
    {
        v.FieldPath("level", levelPath, "Level");
        v.Field("loadWhenActive", loadWhenActive, "Load When Active");
    }
};

// interval/トリガー判定で LevelManager::Load/Unload を呼ぶ常設 System。
class LevelStreamSystem : public ecs::SystemBase { public: void Update() override; };
```

## 9. 初期 Level の指定（プログラム側）

```cpp
// ゲーム状態の初期化で（旧 BattleScene::Initialize 相当）
aq::level::LevelManager::Get().SetStartupLevel("Assets/Levels/Main.level.json");
aq::level::LevelManager::Get().LoadStartup();
```

ゲーム状態層が「どの root Level を読むか」を決め、`LevelManager` が中身のデータ駆動ロードを担う二層構造。

---

## 10. World Partition（空間ストリーミング・オープンワールド）

UE5 の **World Partition** 相当。巨大ワールドをグリッドのセルに分割し、プレイヤー位置からの距離で
セルを自動 Load/Unload する。**セル = Level** を再利用し、World Partition は「どのセルをいつ Load/Unload
するか」を距離で判定する薄いドライバに徹する（新しい生成/破棄機構は持たない）。

### 10.1 データ形式 `.world.json`

```json
{
  "name": "OpenField",
  "grid": {
    "cellSize":     12800.0,   // 1セルの一辺（ワールド単位）
    "loadRadius":   2,         // ソースから何セル分ロードするか
    "unloadRadius": 3          // ヒステリシス（load<unload でチャタリング防止）
  },
  "cells": [
    { "coord": [0,0], "level": "Cells/Cell_0_0.level.json" },
    { "coord": [1,0], "level": "Cells/Cell_1_0.level.json" }
    // 各セル Level = そのセル境界内の entity だけを持つ通常の .level.json
  ],
  "alwaysLoaded": [
    { "level": "Persistent.level.json" }   // 分割対象外（マネージャ・空・音楽等）
  ]
}
```

### 10.2 ランタイム型 / 駆動

```cpp
// ストリーミングの中心。通常はプレイヤー/カメラ。複数可（分割画面・AI）。
struct StreamingSourceComponent : public ecs::IComponent
{
    ecsComponent(aq::level::StreamingSourceComponent);
    float radiusScale = 1.0f;
    template <typename V> void Reflect(V& v) { v.Field("radiusScale", radiusScale, "Radius Scale"); }
};

struct GridCell
{
    int32_t     coordX = 0;
    int32_t     coordZ = 0;        // 水平 2D 分割が定番
    math::AABB  bounds;
    std::string levelPath;         // セル = Level
    LevelId     loaded;            // 未ロード = 無効
};

class WorldPartition
{
public:
    static WorldPartition& Get();
    void Open(std::string_view worldPath);   // .world.json をロード（cells はまだ Load しない）
    void Close();                             // 全セル Unload + 常時ロード解放
    void UpdateStreaming(const std::vector<math::Vector3>& sources);
    LevelId CellAt(const math::Vector3& worldPos) const;
};

// StreamingSourceComponent を集めて WorldPartition::UpdateStreaming を回す常設 System。
class WorldStreamingSystem : public ecs::SystemBase { public: void Update() override; };
```

### 10.3 ストリーミング判定（`UpdateStreaming`）
1. `Foreach<StreamingSourceComponent, TransformComponent>` でソース位置を集める。
2. 各ソース周囲 `loadRadius` セル内の**未ロードセル**を `LevelManager::Load(cell.levelPath)`。
3. どのソースからも `unloadRadius` を超えた**ロード済みセル**を `LevelManager::Unload(cell.loaded)`。
4. `alwaysLoaded` は Open 時に一度だけロード、Close まで保持。
5. 負荷分散のため **1フレームあたり Load 件数に上限**を設ける（将来の非同期化の布石）。

### 10.4 オープンワールド固有の考慮
- **座標精度（Large World）**: `float` ワールド座標は原点から遠いと精度低下（数 km で顕著）。
  対策は (a) `double` 座標化、(b) **World Origin Rebasing**（原点をプレイヤー中心へ定期シフト）。
  現行 `TransformComponent` は `Vector3`(float)。**まず float 安全圏に収める前提で始め、Rebasing は将来 Phase**。
  → §13 未決事項（ワールド想定スケール）で方針確定。
- **ロードスパイク**: セル Load は複数 entity 一括生成。まず 1フレーム Load 上限で緩和、将来は非同期プリロード。
- **チャタリング**: `unloadRadius > loadRadius` のヒステリシス（§10.1）。
- **セル境界跨ぎの参照**: `EntityHandle` の generation で安全に無効化される（既存 ECS と同じ）。

### 10.5 オーサリング（entity→セル振り分け）
- 当面は**ビルド/保存ステップ**で十分: 大 Level（全 entity・ワールド座標）を入力に、各 entity の position
  からセル座標を計算して `Cell_x_z.level.json` へ振り分け出力。
- 手動で最初からセル分割配置も可（`.world.json` の `cells` 手書き）。
- 将来は Level エディタにグリッド可視化 + ドラッグ配置。

---

## 11. 段階導入プラン

### Level 本体
| Phase | 内容 | 検証 | 状況 |
|---|---|---|---|
| **L1** | `LevelId` / `LevelMemberComponent` / Prefab 生成に `onEachCreated` フック追加 | 手動生成 Entity に LevelId が差さる | ✅ 実装済み |
| **L2** | `LevelData`/`SubLevelRef`/`LevelSerializer`/`LevelRegistry`/`LevelManager::Load`（entities のみ） | 単一 `.level.json` からフォレスト生成 | ✅ 実装済み（ビルド検証・ランタイム自己テスト未） |
| **L3** | `Unload`（LevelId 走査破棄）+ generation stale 化 | Load→Unload で該当 Entity のみ消える | ✅ 実装済み（ビルド検証・ランタイム自己テスト未） |
| **L4** | `subLevels` + `loadOnStart` + 再帰 Load/Unload | 親子 Level のカスケード | ✅ 実装済み（ビルド検証・ランタイム自己テスト未） |
| **L5** | `SetStartupLevel`/`LoadStartup` + ゲーム状態層からの配線 | プログラム指定の初期 Level 起動 | ✅ 実装済み（実機起動確認済み） |
| **L6** | `LevelStreamComponent`/`LevelStreamSystem` | コンポーネント駆動の動的ストリーム | ✅ 実装済み（ビルド検証） |
| **L7（任意）** | Level エディタ（Prefab エディタ流用）/ Save | ImGui で Level 編集・保存 | ✅ L7a-c 実装済み（ビルド検証）/ L7d 未（§14） |

### World Partition
| Phase | 内容 | 検証 |
|---|---|---|
| **W1** | `WorldGridSettings`/`GridCell`/`.world.json` パーサ + `WorldPartition::Open/Close` | セル定義を読める |
| **W2** | `StreamingSourceComponent`/`WorldStreamingSystem` + 距離判定で Load/Unload | ソース移動でセルが出入り |
| **W3** | ヒステリシス + 1フレーム Load 上限 + `alwaysLoaded` | 境界往復でチャタリングしない |
| **W4** | オーサリング振り分けツール（Level→セル分割出力） | 大 Level を自動タイル化 |
| **W5（任意）** | 非同期プリロード / World Origin Rebasing / Data Layers / HLOD | 大規模化 |

---

## 12. 旧 Scene 移行計画（`app::IScene` / `app::SceneManager` 破棄）

- `app::SceneManager`（ID 切替のゲーム状態管理）は Level とは責務が異なる
  （ゲーム状態 vs データ駆動コンテンツ）。**役割は残すが名称/実装を刷新**する想定。
- 移行手順（案）:
  1. L1〜L5 完成後、`BattleScene` の Entity 直接生成を `.level.json` へ移す。
  2. `SceneManager::Update` の中身を「現在のゲーム状態が指す root Level を LevelManager で保持」に置換。
  3. `IScene` を薄いゲーム状態インターフェース（入力/UI/カメラ遷移）に縮小、Entity 所有は Level に委譲。
  4. 旧 `Scene.h` の Entity 生成コードを撤去。
- **注意**: 破棄は L5 まで到達し実運用が Level に移ってから。それまでは共存。

## 13. 未決事項

1. **ワールド想定スケール**（World Partition の座標精度方針に直結）:
   - 数百 m 級 → `float` のまま最短実装。
   - 数 km〜数十 km → `double` 化 / World Origin Rebasing を設計初期から織り込む。
2. ゲーム状態層（旧 SceneManager 置換）の最終形（名称・API）。L5 着手時に確定。

---

## 14. L7 Level エディタ 設計メモ（未着手・別環境での継続用）

> Level を組み立てる ImGui エディタの設計方針。**Prefab エディタ（[../ECS/PrefabEditor.h](../ECS/PrefabEditor.h)
> `PrefabEditorPanel`）を最大限流用する**。現状 Level 側（entities[]/subLevels[]）は手書き JSON なので、これを GUI 化する。

### 14.1 位置づけ / 再利用
- Level = 「Prefab ノードのフォレスト + subLevels」。よって **1 エンティティの編集は Prefab エディタと同一 UX**。
- 既存 `PrefabEditorPanel` の資産:
  - `PrefabEditNode`（name + 型消去 `AlignedStorage` components + children）… ノード編集の実体。
  - `DrawTree` / `DrawInspector`（`drawInspectorPtr(void*)` パレット）/ `NodeToJson` / `JsonToNode` … 木編集と JSON 往復。
- **推奨リファクタ**: `PrefabEditorPanel` のノード編集部（ツリー描画・インスペクタ・NodeToJson/JsonToNode）を
  再利用可能なヘルパ（例 `PrefabTreeEditWidget`）へ切り出し、Prefab/Level 両エディタで共有する。
  （切り出さない場合は Level エディタ側で同等ロジックを複製することになる＝二重管理。要リファクタ）
- **前提タスク**: `LevelStreamComponent` を **`FillReflectPtrFns` 対応**にする（L6 では未対応）。
  型消去エディタ（`drawInspectorPtr`/`serializePtr`/`deserializePtr`）を [../ECS/ComponentRegistry.cpp](../ECS/ComponentRegistry.cpp)
  の `FillReflectPtrFns<T>` 相当で Level 側にも設定しないと、エディタのコンポーネントパレットに出ない。

### 14.2 LevelEditorPanel（新規・`aqEngine/Level/LevelEditor.{h,cpp}` 想定）
```cpp
// #ifdef AQ_DEBUG_IMGUI。IDebugRenderable, カテゴリ "Tools"。
class LevelEditorPanel : public IDebugRenderable
{
    // 編集モデル（.level.json に対応）
    std::string                                   name_;
    std::vector<std::unique_ptr<PrefabEditNode>>  entities_;   // 各要素 = 1 トップレベル entity ツリー
    struct SubLevelEdit { std::string path; bool loadOnStart = true; };
    std::vector<SubLevelEdit>                     subLevels_;
    char                                          pathBuf_[260] = "Assets/Levels/New.level.json";
    // 画面: [Entities ツリー(複数 root) | Inspector] + [SubLevels リスト(path + loadOnStart チェック)] + ツールバー
};
```
- **entities**: Prefab エディタのツリー UI をそのまま複数 root で回す。各 root に「Add Entity」。
  各エンティティは (a) インライン編集、または (b) `.prefab.json` 参照（`"prefab": path`）を選べると良い。
- **subLevels**: `path` テキスト + `loadOnStart` チェックボックスの行リスト（Add/Remove）。
- **Save**: `{ name, entities:[NodeToJson...], subLevels:[{level:path, loadOnStart}] }` を `.level.json` へ書き出し。
- **Load**: `.level.json` を parse → `name_` / `JsonToNode` で entities_ / subLevels_ を復元。
- **Load in World（プレビュー）**: `LevelManager::Get().Load(pathBuf_)` で実際にワールドへロード、
  `UnloadAll()`（または当該 LevelId の `Unload`）でクリア。
- 登録: `BattleScene`（または将来のゲーム状態層）で `DebugUI::Register(&levelEditorPanel_)`。

### 14.3 段階（L7 サブフェーズ）
| | 内容 |
|---|---|
| L7a | ✅ `PrefabEditorPanel` のノード編集部を共有関数へ切り出し（[../ECS/PrefabEditNodeOps.h](../ECS/PrefabEditNodeOps.h) / .cpp。`PrefabNodeToJson`/`FromJson`/`DrawTree`/`DrawInspector`/`Remove`。PrefabEditorPanel は委譲＝挙動不変） |
| L7b | ✅ `LevelEditorPanel` 骨組み（複数 root entities ツリー + Inspector + Save/Load）… [LevelEditor.h](LevelEditor.h) / .cpp |
| L7c | ✅ subLevels 行 UI（path + loadOnStart）+ `.level.json` 往復 + Load in World（in-memory 登録→`LevelManager::Load`） |
| L7d | ✅ entity の `.prefab.json` 参照モード（`PrefabEditNode::prefabRef`・Inspector に "Prefab Ref"・JSON `{ name, prefab }` 往復）。✅ Level エディタの Entity は Transform 必須化（`PrefabNodeEnsureTransform`。HTC は実行時 `CollectTypes` が自動付与）。✅ インラインサブLevel（`.level.json` の `subLevels[].level` に文字列=ファイル / オブジェクト=インライン定義）。／ 未: `LevelStreamComponent` の FillReflectPtrFns 対応、override 編集 UI・参照の変更検知（§16） |

> **実装状況（L7a-c 完了・ビルド検証）**: ソリューション Debug|x64 ビルド成功。`Application` が全シーン共通で
> `LevelEditorPanel` を DebugUI に登録（Tools > Level Editor）… [../Core/Application.cpp](../Core/Application.cpp)。
> ノード編集は Prefab エディタと `PrefabEditNodeOps` を共有（単一の真実）。
>
> **追加実装（L7d 一部・ビルド検証）**:
> - **プレハブ参照配置**: `PrefabEditNode::prefabRef`（[../ECS/PrefabEditor.h](../ECS/PrefabEditor.h)）。エディタの **「+ Prefab」ボタン**（`+ Entity` とは別枠）で
>   参照ノードを追加、Inspector の "Prefab Ref" でパス編集。JSON は `{ name, prefab }` で往復（[../ECS/PrefabEditNodeOps.cpp](../ECS/PrefabEditNodeOps.cpp)）。
>   実体は**ロード時に参照先から解決**（変更は再ロード＝§16 D1/D2 で反映）。
> - **Transform 必須化**: Level エディタで作る Entity（新規・子）は `PrefabNodeEnsureTransform` で TransformComponent を必ず持つ。
>   HTC は実行時 `CollectTypes`（[../ECS/Prefab.cpp](../ECS/Prefab.cpp)）が Transform→HTC を自動付与するため常に成立。Prefab エディタ側は従来どおり（`ensureTransform=false`）。
> - **インラインサブLevel**: `.level.json` の `subLevels[].level` が **文字列=外部ファイル参照 / オブジェクト=インライン定義**。
>   `LevelSerializer` が再帰構築し `SubLevelRef::inlineData` に保持、`LevelManager::LoadInline` が生成（[LevelSerializer.cpp](LevelSerializer.cpp) / [LevelManager.cpp](LevelManager.cpp)）。
>   **エディタでも作成可能**: subLevels の「+ File」（外部参照）/「+ Inline」（その場定義）ボタン。インラインは name + 配下 entities を
>   同じツリー UI（`DrawEntityList`）で編集し `{ level:{ name, entities }, loadOnStart }` で往復（[LevelEditor.cpp](LevelEditor.cpp)）。
> **残（L7d）**: `LevelStreamComponent` の FillReflectPtrFns 対応。override 編集 UI は §16 R2。参照の変更検知（D1/D2）は §16=実装済み。

---

## 15. ロード分散 / 非同期ロード（ローディング画面対応）設計メモ（未実装・優先度=次点）

> **背景（現状の制約）**: `LevelManager::Load` は `RequestDeferredBuild` を **1 コマンド積むだけ**で、
> 次の `FlushCommands` で**そのツリー全部（サブLevel 含む）をメインスレッドで同期一括生成**する。
> JSON パース（`LevelSerializer`/`PrefabSerializer`）も未キャッシュ時はメイン同期。
> → 大きな Level を差すと**そのフレームでスパイク**し、ローディング画面を出しても実ロード中は固まる。
> フレーム分割も件数上限も進捗通知も**無い**（設計 §10.3/§10.4・W5 の将来課題に相当）。

### 15.1 採用方針：まず「段階1＝フレーム分割ロード + 進捗」

メインスレッドのまま、生成を複数フレームに分散し進捗を返す。これで**プログレスバー付きローディング画面が成立**する。

**API 案**
```cpp
struct LevelLoadHandle
{
    LevelId  id;
    uint32_t total = 0;      // 生成予定エンティティ総数
    uint32_t built = 0;      // 生成済み
    bool     IsDone()  const { return built >= total; }
    float    Progress() const { return total ? float(built) / float(total) : 1.0f; }
};

// 非同期（分割）ロード。entitiesPerFrame ずつ生成する。
LevelLoadHandle LevelManager::LoadAsync(std::string_view pathOrId, LevelId parent = {},
                                        uint32_t entitiesPerFrame = 64);
```

**実装スケッチ**
- `LevelData` のフォレストを **平坦化した生成ジョブキュー**にする（各要素 = 型リスト + `JsonValue` + 親ハンドル + `levelId`）。
  親子は生成順依存（HTC `SetParent` は親 Entity ハンドルが必要）なので **親→子の順**でキュー化し、生成済みハンドルを覚えて子に渡す。
- 進行は専用の漸進ビルダ（`LevelManager` 内キュー）を**毎フレーム `entitiesPerFrame` 件**処理。生成は従来の
  `RequestCreateEntityFromTypes`/`create` プリミティブを流用（`LevelMemberComponent` 注入 + `levelId` stamp は同じ）。
- `built`/`total` を更新。ゲーム状態層が毎フレーム `Progress()` を UI に反映し `IsDone()` で遷移。
- **最小実装（粗い分割）**: ツリー平坦化が重ければ、まず**「1 フレーム = トップレベル entity 1 本（サブツリー丸ごと）」**の粒度で分割開始 → 後でノード粒度へ精緻化。
- 既存 `Load`（同期一括）は残す（小さい Level / 起動時用）。`LoadAsync` を併設する。

**留意点**
- `InstantiatePrefabTree` は「1 ツリー = 1 コマンド」前提。フレーム分割にはツリーを跨いで中断・再開できる
  **ノード単位の生成経路**が要る（親ハンドル保持しながら）。ここが新規実装の肝。
- `LevelStreamSystem` / World Partition のセル Load も、この `LoadAsync` に載せ替えれば**ストリーミングのスパイクも緩和**できる（§10.3 の「1 フレーム Load 上限」を包含）。

### 15.2 段階2：非同期プリロード（本格・段階1の上に載せる）

- バックグラウンドスレッドで **JSON パース → `LevelData` 構築**（参照展開含む）を先行実施（`LevelData` は不変・共有なのでスレッド跨ぎ安全）。
- **Entity 生成はメインに残す**（ECS はスレッド安全性の都合でメイン生成が安全）＝段階1のフレーム分割をそのまま使う。
- `LevelRegistry` に「バックグラウンドで parse 中」の状態を持たせ、完了後に段階1の生成キューへ投入。

### 15.3 段階（サブフェーズ）
| | 内容 |
|---|---|
| A1 | ✅ ノード単位の中断可能生成（`ecs::InstantiatePrefabNode`＝子を再帰しない1ノード生成を Prefab から公開・[../ECS/Prefab.cpp](../ECS/Prefab.cpp)）。`LevelData` フォレスト+loadOnStart サブLevel を `BuildJob` 列にフラット化（`FlattenLevel`/`FlattenNode`・親ジョブ index 保持） |
| A2 | ✅ `LevelManager::LoadAsync` + `LevelLoadHandle`（`entitiesPerFrame` ずつ・`shared_ptr<LevelLoadProgress>` で live 進捗） |
| A3 | ✅（土台）ローディング画面用の進捗 API（`Progress()`/`IsDone()`/`Built()`/`Total()`）。Level エディタに "Load Async" + `ImGui::ProgressBar` の検証表示。ゲーム状態層への本配線は利用側で |
| A4 | 未: `LevelStreamSystem` / World Partition セル Load を `LoadAsync` へ載せ替え（スパイク緩和） |
| A5 | 未: 段階2：バックグラウンド JSON パース（`LevelData` 先行構築）を A2 の生成キューへ接続 |

> **実装状況（段階1=A1-A3 完了・ビルド検証）**: ソリューション Debug|x64 ビルド成功。… [LevelManager.h](LevelManager.h) / [.cpp](LevelManager.cpp)
> - **安全点での即時生成**: `LevelManager::Tick` を **`Application::Update` の `EntityContext::Update()` 後**（ForEach/並列システム外・単一スレッド）で呼び、
>   そこで `EntityContext::CreateEntityFromTypes`（即時）を使って `entitiesPerFrame` 件ずつ生成する（[../Core/Application.cpp](../Core/Application.cpp)）。
>   ※ システム更新フェーズ中の即時生成は不可（並列 ForEach と競合）。D2 ポーリングも同 Tick に統合し、`LevelStreamSystem` からの Tick 呼びは撤去。
> - **フラット化**: フォレスト＋loadOnStart サブLevel を親→子順の `BuildJob` 列に。各ジョブは自身の `levelId` を持ち、生成時に `LevelMemberComponent` へ stamp。
>   サブLevel の slot は LoadAsync 時に採番（読み込み中も IsLoaded=true）。循環は深さ上限 32 でガード。
> - **進捗**: `LevelLoadHandle`（`shared_ptr<const LevelLoadProgress>`）で `built/total`・`done` を live に読める。
> - 既存の同期 `Load` は温存（起動/リロード/ストリーム/プレビュー用）。`LoadAsync` は別 API。
>
> **未（段階1 残 / 段階2）**: A4（ストリーム/セル Load の LoadAsync 化）、A5（BG パース）、読み込み中の Unload 競合の厳密化。

---

## 16. 参照の変更検知・反映 / override（Prefab・Level 情報の上書き）設計メモ（一部実装・要方針確認）

> 目的（要望）: Prefab / サブLevel を**参照として保持**し、参照先が変わったら**検知して反映**したい。
> 必要なら参照先の情報を**上書き（override）**したい（Unity のプレハブ運用に近い）。

### 16.1 現状（実装済みの土台）
- **参照の保持**: エディタは `PrefabEditNode::prefabRef`（entity の prefab 参照）を持ち、JSON `{ name, prefab }` で往復（L7d）。
  サブLevel も `subLevels[].level`（文字列=ファイル参照 / オブジェクト=インライン）を持つ。
- **反映（ロード時）**: 参照は**ロード時に参照先ファイルから解決**される。
  - `PrefabSerializer::FromJson` は呼び出しごとに `LoadContext`（`parseCache`）を作り直す＝**毎回ファイルを読み直す**。
    → エディタ「Load in World」は毎回最新の prefab 内容を反映する。
  - overrides 意味論（deep merge / added / removed・children は name 同定）は **PrefabSerializer に実装済み（§7.3）**。
    ＝ `{ prefab, overrides }` を**手書き JSON なら今も反映される**。
- **未反映になる箇所**: `PrefabRegistry` / `LevelRegistry` は**プロセス寿命でキャッシュ**する。
  `LevelManager::Load(path)` は `LevelData`（prefab 参照は**parse 時に展開済み**）をキャッシュするので、
  **参照先ファイルを更新してもキャッシュが残る限り反映されない**。

### 16.2 変更検知・反映（3 段階・方針確認したい）
| 段階 | 内容 | 実装量 |
|---|---|---|
| **D1（手動）** | エディタ/デバッグに **"Reload References"** ボタン: `PrefabRegistry`/`LevelRegistry` の `Clear()` → 対象 Level を Unload→Load し直す。最小で「変更を反映」を達成 | 小 |
| **D2（半自動）** | ファイルの **mtime/ハッシュを記録**し、フレーム先頭 or 明示ポーリングで差分を検知→該当 Level を reload（内部は D1） | 中 |
| **D3（自動監視）** | OS のファイル監視（`ReadDirectoryChangesW`）で `.prefab.json`/`.level.json` の変更を検知→自動 reload | 中〜大 |

- **反映の実体は「Unload → Load し直し」**が現実解。**ライブ instance へ差分パッチ**（生成済み Entity の該当コンポーネントだけ更新し
  ランタイム状態を保持）は Unity 相当で**非常に大きい**ため、初期は非対象。
  → まず「該当 Level（またはその Entity 群）を作り直す」。ランタイム状態が消える点は許容 or 対象を絞る。
- どの Entity がどの参照由来かは、`LevelMemberComponent`（所属 Level）に加え **参照元 prefabPath を持つ軽量タグ**を付ければ
  「この prefab を使う Entity だけ作り直す」まで絞れる（D2/D3 の精緻化）。

### 16.3 override（参照先情報の上書き）
- **ランタイムは実装済み**（§7.3）。`{ prefab, overrides:{ components, addedComponents, removedComponents, children, removedChildren } }`。
- **未実装 = エディタの override 編集 UI**。手順案:
  1. 参照ノード（`prefabRef` 有）を選択したら、**参照先を解決してベース構成を表示**（`PrefabSerializer` でロード→読み取り専用ツリー）。
  2. その上で値を変えた項目だけを **`overrides.components`（deep merge）** として `PrefabEditNode` に保持（新フィールド `util::JsonValue overrides;`）。
  3. 追加/削除は `addedComponents` / `removedComponents`、子は name 同定で `overrides.children` / `removedChildren`。
  4. 保存時 `{ name, prefab, overrides }` を書き出す（ランタイムはそのまま解釈）。
- **サブLevel の override** も同型（`subLevels[].overrides` を Level ロードに適用）だが、Level 側の override 意味論は未実装
  （現状サブLevel は参照 or インラインのみ）。必要なら prefab と同じ `ApplyPatch` を Level にも展開する。

### 16.4 段階（サブフェーズ）
| | 内容 |
|---|---|
| R1 | ✅ **D1**: `LevelManager::ReloadAll`（`PrefabRegistry`/`LevelRegistry` を Clear → ファイル由来 root を Unload→Load）。Level エディタに "Reload Refs" ボタン |
| R2 | ✅（v1）エディタ override 編集 UI。参照ノードの `components` を **override（deep-merge/追加）** として編集、`removedComponents` で削除指定。`{ prefab, overrides:{ components, removedComponents } }` で往復（[../ECS/PrefabEditNodeOps.cpp](../ECS/PrefabEditNodeOps.cpp)）。／ 未: 参照先の**ベース構成を解決表示**、**フィールド単位の差分**（現状は「コンポーネント丸ごと override」）、**子（children）override** |
| R3 | ✅ **D2**: `LevelManager::Tick`/`PollFileChanges`（0.5 秒間引きで root Level ファイルの mtime を確認→変更で reload）。エディタに "Auto" トグル（`SetAutoReload`）。`LevelStreamSystem::Update` から `Tick` |
| R4 | 未: **D3**: OS ファイル監視（`ReadDirectoryChangesW`）での自動 reload |
| R5（大）| 未: ライブ instance への差分パッチ（作り直さずランタイム状態保持）※要否は別途判断 |

> **実装状況（R1=D1・R3=D2 完了・ビルド検証）**: ソリューション Debug|x64 ビルド成功。… [LevelManager.cpp](LevelManager.cpp) / [LevelEditor.cpp](LevelEditor.cpp) / [LevelStreamSystem.cpp](LevelStreamSystem.cpp)
> - **D1（手動）**: "Reload Refs" ボタン → `ReloadAll`。キャッシュを捨てるため**プレハブ編集も反映**される。
> - **D2（自動・mtime ポーリング）**: "Auto" ON で 0.5 秒毎に root Level ファイルの更新時刻を確認、変わっていたら
>   キャッシュ Clear + 当該 Level を作り直す（参照プレハブも読み直される）。合成キー（`<inline>`/`<...preview...>`）は監視対象外。
> - **既知の制約**: 変更検知の対象は**ロード済み root Level ファイルの mtime のみ**。
>   「プレハブファイルだけを編集」した場合の自動検知は未（プレハブ依存の追跡が要る＝D2+）。当面は **D1 の手動 Reload で対応**。
>   reload の実体は**Unload→Load の作り直し**なのでランタイム状態は保持されない（ライブパッチ R5 は別途）。
>
> **追加実装（R2 v1・ビルド検証）**: 参照ノードの Inspector に override 編集を追加。
> - `PrefabEditNode` に `removedComponents`（[../ECS/PrefabEditor.h](../ECS/PrefabEditor.h)）。参照ノードでは `components` を **override** として扱い、
>   `+ Add Component` で override/追加、`+ Remove Component` で `removedComponents` を編集。保存は `{ prefab, overrides:{ components, removedComponents } }`。
> - ランタイムは `PrefabSerializer::ResolveNode/ApplyPatch`（§7.3）がそのまま適用（deep-merge / 追加 / 削除）。Prefab エディタ内のネスト参照でも同じ UI が使える。
> - **未（R2.5）**: 参照先のベース構成を解決表示、**フィールド単位の差分**（現状は該当コンポーネントを丸ごと override）、children override。
>
> **残**: R2.5（フィールド差分・ベース表示・子 override）、R4（OS 監視 D3）、R5（ライブパッチ）、プレハブ単独編集の自動検知（D2+）。
