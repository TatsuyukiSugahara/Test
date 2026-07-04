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
| **L4** | `subLevels` + `loadOnStart` + 再帰 Load/Unload | 親子 Level のカスケード | 未 |
| **L5** | `SetStartupLevel`/`LoadStartup` + ゲーム状態層からの配線 | プログラム指定の初期 Level 起動 | 未 |
| **L6** | `LevelStreamComponent`/`LevelStreamSystem` | コンポーネント駆動の動的ストリーム | 未 |
| **L7（任意）** | Level エディタ（Prefab エディタ流用）/ Save | ImGui で Level 編集・保存 | 未 |

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
