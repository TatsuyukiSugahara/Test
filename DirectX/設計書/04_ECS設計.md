# ECS 設計ドキュメント

> 対象コミット: `144ae8a` / 最終更新: 2026-07-03

対象: `aqEngine/ECS/`, `aqEngine/Component/`
関連: [Prefab設計.md](Prefab設計.md)
参考: アーキタイプ ECS(https://qiita.com/harayuu10/items/e15b02e3b0f3081d729b)

**アーキタイプ方式のデータ指向 ECS**。同一コンポーネント構成(Archetype)のエンティティを Chunk に SoA(Structure of Arrays)で密に並べ、System が型でクエリして並列に走査する。

---

## 0. 設計の全体像

```
EntityContext（唯一のシングルトン・統括窓口）
   ├─ EntityManager   … エンティティ生成/破棄/コンポーネント追加削除/クエリ
   │     ├─ chunkList_ : Chunk[]        … Archetype ごとの SoA バッファ
   │     ├─ slots_     : EntitySlot[]   … EntityID → 現在位置(世代付き)
   │     └─ pendingCommands_            … 遅延構造変更コマンド（並列 System から安全に積む）
   └─ SystemManager   … System 登録・依存解決・波(wave)並列実行
```

**中核概念**:
- **Entity** … 実体はデータを持たず、`EntityHandle{ id, generation }` で参照される。世代で dangling を検出。
- **Component** … `IComponent` 派生 + `ecsComponent(型名)` マクロ。データのみ(POD 的)。
- **Archetype** … コンポーネント型の集合。同一 Archetype のエンティティが同じ Chunk に入る。
- **Chunk** … 1 Archetype 分の SoA メモリブロック。コンポーネントごとに連続配置(キャッシュ効率)。
- **System** … `SystemBase::Update()`。`Foreach<Cs...>` で対象コンポーネントを持つ全エンティティを走査。

**構造変更の安全性**: 生成/破棄/Add/Remove Component は**遅延コマンド**として積み、`FlushCommands()`(System 更新後)で一括適用。これにより並列 System 実行中・`ForEach` 走査中の chunk 再確保を防ぐ。

---

## 1. 型情報・コンポーネント基盤

| ファイル | 責務 |
|---|---|
| `TypeInfo.h` | **コンパイル時型情報**。`TYPE_INFO(T)` マクロが `GetTypeName`/`GetTypeHash`(FNV-1a)を生成。`TypeInfo` は hash/size/align + 関数ポインタ(destructor/mover/constructor)を持つ。`Create<T>()` で型から生成、`Construct(p)` で生メモリへ placement-new(trivial 型も構築して未初期化を防ぐ)。実行時 Archetype 構築(Prefab)経路で必須。 |
| `IComponent.h` | **コンポーネント基底**。`struct IComponent{}` + `ecsComponent(T)` マクロ(= `TYPE_INFO`)。`MAX_COMPONENT_COUNT = 16`(1 Archetype の最大コンポーネント数)。`IsComponent<T>` 判定。 |
| `ComponentArray.h` | Chunk 内の 1 コンポーネント型の連続領域を指す軽量ビュー(`begin_` + `size_`、`operator[]`)。`ForEach` が Chunk ごとに取得。 |
| `AlignedStorage.h` | over-aligned コンポーネント用のアライメント補助。 |
| `EntityDebugTag.h` | debug ビルドで全 Entity に自動付与する名前タグ(`AQ_DEBUG_IMGUI`)。エディタ表示用。 |

---

## 2. エンティティ・アーキタイプ・チャンク

| ファイル | 責務 |
|---|---|
| `Entity.h` | **識別子と軽量ラッパ**。`EntityID`(uint32)/`EntityHandle{ id, generation }`(安全ハンドル)/`EntityLocation{ chunkIndex, index }`(内部の物理位置・非公開)/`EntitySlot`(ID→位置+世代+alive+freeList)。`Entity` クラスは `EntityManager* + EntityHandle` を保持する値渡し可能ラッパで、generation チェック済み API(`GetComponent/AddComponent/Destroy/IsValid`)のみ公開。`{chunkIndex,index}` を隠蔽。 |
| `Archetype.h` | **コンポーネント型集合**。`typeList_[MAX_COMPONENT_COUNT]` を hash 昇順ソートで保持。`AddType`/`RemoveType`/`HasType`/`IsIn`(包含判定=クエリの核)/`GetOffset`(SoA ブロック先頭バイト。padding 込み・maxAlign 境界)。すべて `constexpr`。SoA レイアウトの寸法計算(`GetArchetypeMemorySize` = capacity 倍で全体サイズ)を担う。 |
| `Chunk.h` / `Chunk.cpp` | **1 Archetype 分の SoA バッファ**。`begin_`(over-align 対応カスタムデリータ付き)に「コンポーネント A を capacity 個 → B を capacity 個 …」と並べる。`CreateEntity`(行追加+EntityID 記録)/`DestroyEntitySwap`(swap-remove)/`MoveEntitySlot`(Add 方向の別 chunk 移動)/`MoveEntityRemoveArchetype`(Remove 方向)。`GetComponent<T>(loc)`(型版)/`GetComponentByType`(実行時 TypeInfo 版・Prefab 用)/`GetComponentArray<T>`。 |

**SoA レイアウトのポイント**: `GetComponent<T>` は `offset = archetype.GetOffset<T>() * capacity_` + `sizeof(T) * index` で位置を計算。同じ型のインスタンスが連続するためキャッシュヒット率が高い。swap-remove で削除穴を末尾で埋め、密度を維持。

---

## 3. マネージャ・統括

| ファイル | 責務 |
|---|---|
| `EntityManager.h` / `EntityManager.cpp` | **ECS の中枢**。`chunkList_`/`slots_`(ID→位置・freeList)/`pendingCommands_` を所有。<br>**生成**: `CreateEntity<Args...>`(即時・ForEach 外)/`RequestCreateEntity`(遅延)/`CreateEntityFromTypes`/`RequestCreateEntityFromTypes`(実行時 TypeInfo・Prefab)/`RequestDeferredBuild`(複数 Entity を 1 コマンドで生成 = Prefab ツリー)。<br>**構造変更**: `AddComponent`/`RemoveComponent`/`RequestDestroyEntity`(全て遅延コマンド)。`FlushCommands()` で一括適用。<br>**クエリ**: `GetView<Cs...>()` → `EntityView`。<br>**並行制御**: `iterationMutex_`(shared_mutex。ForEach=リード、構造変更=ライト)+ `commandMutex_`。`isInUserCallback_` で onCreated/onAdded 中の再入構造変更を禁止。 |
| `EntityContext.h` / `EntityContext.cpp` | **唯一のシングルトン窓口**。`EntityManager` + `SystemManager` を所有。`Update()`(System 更新 → FlushCommands)。`CreateEntity<Args...>` は **TransformComponent + HierarchicalTransformComponent を必須基底として自動付与**(片方だけ指定は static_assert 禁止)、debug では `EntityDebugTag` も注入。親子階層操作(`SetParent`/`GetChildren`/`DetachParent`)、System 登録(`AddSystem`/`AddDependency`/`FinalizeRegistration`)を集約。 |
| `EntityView.h` | **クエリビュー**。`chunkIndices_`(Chunk* でなくインデックスを保持 = chunk 再確保後の dangling 防止)。`ForEach(func)` は shared_lock(IterationGuard・RAII)を取り、対象 Chunk を走査して `func(entity, &components...)` を呼ぶ。実装は `EntityManager.h` 末尾(完全定義が必要なため)。 |
| `ECS.h` | **`Foreach<Cs...>(func)` グローバルヘルパ**。`EntityContext::Get().GetView<Cs...>().ForEach(...)` の薄いラッパ。System 実装が主に使う。 |

**レビュー観点(並行性)**: `ForEach` 中の構造変更が遅延コマンド経由で確実に後回しになるか。`iterationMutex_`(shared)と `commandMutex_` のロック順序、`isInUserCallback_` の再入防止。`slots_` の freeList と generation 更新の整合(破棄→再利用で世代が上がるか)。

---

## 4. System(登録・依存・並列実行)

| ファイル | 責務 |
|---|---|
| `System.h` | **`SystemBase`**(`Update()` 純粋仮想 + debug 用 `DebugRender*`)と **`SystemManager`**。`AddSystem<T, Deps...>`(重複登録は依存追加のみ)/`AddDependency<Sys, Dep>`/`GetSystem<T>`(dynamic_cast)/`HasSystem`。`SystemEntry` は system + 依存インデックス + 実行 level を持つ。 |
| `System.cpp` | **スケジューリングと並列実行**。`BuildSchedule()` は Kahn のトポロジカルソートで実行順を確定し、循環依存を assert 検出。各 System に **level**(= max(依存 level)+1)を割り当て。`Update()` は **level(wave)ごとに全 System を `ThreadPool::Submit` で並列実行**し、wave 完了を全 future の `get()` で待つ(次 wave へ)。例外は全完了後に最初の 1 つを rethrow。プロファイラで System 名別に計測。 |

**並列モデル**: 依存の無い System は同じ wave で並列。依存があれば後の wave へ。構造変更は遅延コマンドなので、並列実行中に他 System のデータ配置が壊れない(FlushCommands は wave 完了後の `EntityContext::Update` 内)。

---

## 5. コンポーネント登録・シリアライズ

| ファイル | 責務 |
|---|---|
| `ComponentRegistry.h` / `.cpp` | **型名 ↔ TypeInfo/生成関数の実行時レジストリ**。`RegisterCoreComponents()` でコアコンポーネントを登録(ビルド構成を問わず・JSON シリアライズに必要)。Prefab の名前解決・実行時生成の基盤。 |
| `TypeInfo.h`(前掲) | 実行時生成の構築関数を供給。 |
| `ImGuiFieldVisitor.h` | コンポーネントのフィールドを ImGui で編集する visitor(エディタ)。 |
| `JsonFieldVisitor.h` | コンポーネントのフィールドを JSON へ/から変換する visitor(シリアライズ)。 |

---

## 6. Prefab(データ駆動生成)

詳細は [Prefab設計.md](Prefab設計.md)。

| ファイル | 責務 |
|---|---|
| `Prefab.h/.cpp` | **Prefab データ**(コンポーネント構成 + 初期値 + 子ツリー)。`RequestDeferredBuild` で複数 Entity を親子付きで一括生成。 |
| `PrefabRegistry.h/.cpp` | Prefab の登録・名前解決。 |
| `PrefabSerializer.h/.cpp` | Prefab の JSON シリアライズ/デシリアライズ(`JsonFieldVisitor` 利用)。 |
| `PrefabEditor.h/.cpp` | Prefab 編集の ImGui パネル(`PrefabEditorPanel`。全シーン共通・アプリ寿命)。 |
| `SpawnSystem.h/.cpp` | Prefab の実体化(スポーン)を担う System。 |

---

## 7. 階層・シーン System(`ECS/` + `Component/`)

| ファイル | 責務 |
|---|---|
| `SceneHierarchySystem.h/.cpp` | debug ビルドのシーン階層ビュー(エンティティツリーを ImGui 表示)。 |
| `Component/HierarchicalTransformComponent.h/.cpp` | **親子付き Transform**。親のワールド行列を継承。`SetParent`/`DetachParent` の実体。全 Entity の必須基底。 |
| `Component/TransformComponentSystem.h/.cpp` | ローカル Transform コンポーネントとその更新 System。 |
| `Component/AnimationComponentSystem.h/.cpp` | スケルタルアニメーション更新 System(`AnimationClip` 適用)。 |
| `Component/BodyComponentSystem.h/.cpp` | **描画メッシュのコンポーネント + `RenderSystem`**。`BuildRenderFrame()` で ECS を走査し `RenderItem` を `RenderFrame` へ push(**フラスタムカリング**もここ)。オクリュージョンテスターも保持。 |
| `Component/OceanComponent.h/.cpp` | 海(Gerstner 波)コンポーネント。 |
| `Component/TerrainComponent.h/.cpp` | 地形(ハイトマップチャンク)コンポーネント。 |
| `Component/DecalComponent.h/.cpp` | 投影デカールコンポーネント。 |

**ECS とレンダリングの接点**: `RenderSystem`(`BodyComponentSystem.cpp`)が ECS ↔ Rendering の橋。System 依存で `RenderSystem` は `HierarcicalTransformSystem`/`AnimationSystem` の後(ワールド行列・ボーン確定後)に走る。

---

## 8. データフロー(1 フレーム)

```
EntityContext::Update()                       ... Application::Update 内
  ├─ SystemManager::Update()                  ... wave 並列実行
  │     wave 0: HierarcicalTransform, Animation, Spawn ...
  │     wave 1: RenderSystem (依存の後)  → BuildRenderFrame 用の下ごしらえ
  │        各 System が Foreach<Cs...> でコンポーネント走査（構造変更は遅延コマンドで積む）
  └─ EntityManager::FlushCommands()           ... 生成/破棄/Add/Remove を一括適用
```

その後 `Application::Render` で `RenderSystem::BuildRenderFrame()` が `RenderFrame` を組み立てる。

---

## 9. レビュー時のチェックポイント

- [ ] **世代の健全性**: 破棄→スロット再利用時に generation が上がり、旧 `EntityHandle` が `IsValid()==false` になるか。外部が `EntityID`(世代なし)を保持していないか。
- [ ] **SoA レイアウト**: `Archetype::GetOffset` の padding/maxAlign 計算が `Chunk::GetComponent` のアドレス計算と一致するか(over-aligned コンポーネント含む)。
- [ ] **構造変更の遅延徹底**: 即時 `CreateEntity`/`AddComponent` が ForEach 中・コールバック中に呼ばれないか(assert で守られているか)。
- [ ] **クエリの dangling 回避**: `EntityView` が Chunk* でなくインデックスを保持し、chunk 再確保後も安全か。
- [ ] **並列 System の競合**: 同 wave の System が同じコンポーネントを書き換えないか(依存で直列化すべき箇所が漏れていないか)。トポロジカルソートの循環検出。
- [ ] **必須基底の一貫性**: TransformComponent + HierarchicalTransformComponent のペア強制(static_assert)、debug の EntityDebugTag 注入が全生成経路(typed / 実行時 TypeInfo / Prefab)で揃っているか。
- [ ] **MAX_COMPONENT_COUNT(16) 超過**: 実行時生成経路で無言切り捨てでなく診断エラーになるか。
