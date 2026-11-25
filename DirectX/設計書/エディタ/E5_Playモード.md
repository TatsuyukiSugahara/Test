# aqEngine エディタ E5: Edit / Play / Pause

> 対象コミット: d993cda / 最終更新: 2026-11-25(エディタ全体設計.md の旧 8.5 節と 11.3 節の該当フェーズを分割して移した。本文は分割前のまま。各節の照合コミットは節の「現状」に記載)
> 全体の構成・到達点・フェーズ一覧・未決事項は [エディタ全体設計](../エディタ全体設計.md)。節番号(4.4 など)は分割前の番号のまま残している。

## 8.5 E5 の詳細(2026-11-20 追記)

8.1 の遷移(再生のたびにプレビューを置き換えて「開始待ち」を挟む)は、下の「決定」で置き換える。8.1 の表は当初案として残す。

### 現状(調査結果。3898d87 で確認)

| 項目 | 実コード |
|---|---|
| System の実行 | `SystemManager::Update`([System.cpp](../../aqEngine/ECS/System.cpp))が wave ごとに全 System を `ThreadPool` へ出す。`EntityContext::Update()` はその後に `FlushCommands` する。モードの概念は無い |
| エンジン標準の System | `AppHost::Register` が `HierarcicalTransformSystem`、`AnimationSystem`、`SpawnSystem`、`LevelStreamSystem`、`ParticleSystem`、`RenderSystem`(と `AQ_DEBUG_IMGUI` で `SceneHierarchySystem`)を登録する。ゲームは `OnRegister` で 9 個(`aq::sound::SoundSystem` を含む)を足す |
| フレームの順序 | `AppHost::Update` は 5.3 の表と少し違う。エディタのフック `OnBeginFrame` は `InputManager::Update` の前、`OnBuildUI` / `OnEndFrame` は `Render()` の中(ゲームの描画コマンドを組んだ後、提出の前)にある。Level エディタのプレビュー置換(`ProcessPreviewRequest`)はゲーム用コンテキストの `DebugRender` の中で走る |
| 起動の順序 | `Engine::Initialize` が `AppHost::Initialize`(中で `module_->OnInitialize`)→ `AppHost::Register`(中で `module_->OnRegister` と `FinalizeRegistration`)の順に呼ぶ。Editor は成功後に `SetEditorLayer` して `RunGame` |
| GameModule の開始処理 | `OnInitialize`([GameModule.cpp](../../Game/Application/GameModule.cpp))がミニマップのパイプライン、`GameInput::Initialize`、`GameFlow::Create` + `Initialize`、メインのパイプライン、BGM の `OpenStream` + ループ再生を順に行う。`GameFlow::Get()` を呼ぶのは GameModule だけ(`OnUpdate` と `OnInitialize`) |
| GameFlow | `Initialize` が画面の登録(`UIScreenManager::Register` は同名を上書きする)、セッションのエンティティの即時生成、`BootState` への遷移を行う。`Release` は `Finalize`(セッションの破棄を予約)して delete する。ステージの破棄(`DestroyStageWorld`)はリザルト → タイトルの経路でしか呼ばれない |
| ワールドの外の状態(8.3 の TODO) | 可変の静的変数はシングルトンの instance 3 つ(`GameModule`、`GameFlow`、`GameInput`)だけ。それ以外は `GameFlow` と状態クラスのメンバ(ステージのハンドル群、`LevelLoadHandle`、ロード中のワーカーの `future`)、`GameModule` のメンバ(`bgmStream_`、`minimapBakeRequested_`)、エンジンに書いた値(メイン / オフスクリーンのカメラの位置・Far・FOV・Up・正射影、UI のスタック、パッドのトリガー抵抗と振動、`InstancedStaticMesh` の名前付き登録) |
| ロード中のワーカー | `LoadingState` は `ThreadPool::Submit` の `future` を持つ。`Submit` は `packaged_task` なので `future` の破棄は待たない。タスクは値だけを捕まえ(パスと ID)、ECS に触れないので、状態を捨てても安全 |
| `InstancedStaticMesh` | `RegisterFromData` は同名が登録済みなら既存を返す(エンジンのグローバルな登録で、Play の停止では消えない)。ステージは今 1 つなので害は出ていない |
| UI のスタック | `UIScreenManager` の操作は `Push` / `Pop` / `Replace` / `Back` で、すべて保留して `Update` の末尾で処理する。スタックを一度に空にする API は無い |
| サウンド | `SoundEngine` に `PauseAll` / `ResumeAll` / `StopAll` がある。再生中の数を返す API は無い |
| カメラ | `Camera` は値で持つ(`CameraManager` が `Main` と `Offscreen` を持つ)。位置・Near・Far の取得はあるが、注視点・Up・画角・正射影の取得は無い |
| エンティティの列挙 | 全エンティティを集める API は無い。型条件が空の `GetView<>().ForEach` で全エンティティを回れる。`CreateEntity` は `TransformComponent` を自動で付けるが、`CreateEntityFromTypes` は付けない。`EntityHandle` は世代を持ち、`==` がある |
| サウンドの層 | `AudioDirector`(オーサリング層)は `Engine` が毎フレーム `SoundEngine::Update` の後に `Update` する。終わったインスタンスを `IsPlaying()` で判定して回収し、仮想化されたインスタンスを実ボイスへ戻す |
| 毎フレーム書く描画の設定 | `GameModule::OnUpdate` が影の中心(`SetSceneCenter`。`OnInitialize` のプリセットでは `(0, 3, 0)`)とモーションブラーの強さ(`SetStrength`、既定は 0)を毎フレーム書く |
| 起動モード | `LaunchOptions` に `-mode=play|edit` があるが、受け取るだけで使っていない |

### 決定

ユーザー決定(2026-11-20): **再生はゲームを最初から(Game.exe と同じく Boot から)始める。** 編集中の Level のプレビューは Edit 中の表示用で、再生中はアンロードする。**Editor.exe は Edit で起動する。**

E5 を 2 段に分ける。

| 段 | 内容 |
|---|---|
| E5a | エンジンの口。`RunMode`、System のモード宣言、`EntityContext::Update(mode)`、`AppHost` のモードによる更新の制御、`IAppModule::OnPlayBegin` / `OnPlayEnd`、GameModule の開始処理の移動。Editor はまだ Play で起動する(ランタイムと同じ動き) |
| E5b | Editor の再生・一時停止・停止。ツールバー、遷移の手順(`PlayModeController`)、停止時の戻し、エンジンの補助 API、自己テスト。Editor を Edit で起動に変える |

**1. モードと System の宣言(E5a)**

- `aqEngine/Core/RunMode.h`(新規。実装時のユーザー決定で `ECS/` から `Core/` へ。名前空間は `aq`)に `enum class RunMode : uint8_t { Edit, Play, Pause }` と、ビットの組 `RunModeMask`(`Edit = 1`、`Play = 2`、`Pause = 4`、`All = 7`。`|` 演算子と `Contains(mask, mode)` を付ける)を置く。
- `SystemBase` に `virtual RunModeMask GetRunModes() const { return RunModeMask::Play; }` を足す。既存のゲームの System は変更しない。
- `SystemManager::Update(RunMode mode)` は、`GetRunModes()` に `mode` を含まない System を wave に出さない。wave の順序はそのまま使う(飛ばした System に依存する System は、順序の制約が消えるだけ)。
- `EntityContext::Update(RunMode mode = RunMode::Play)` は、System の後の `FlushCommands` を**モードに関係なく必ず行う**。
- エンジンの System の宣言(8.2 の表のとおり):

| System | 宣言 |
|---|---|
| `HierarcicalTransformSystem`、`RenderSystem`、`SceneHierarchySystem` | `All` |
| `ParticleSystem` | `Edit | Play` |
| `AnimationSystem`、`SpawnSystem`、`LevelStreamSystem`、`aq::sound::SoundSystem` | 既定(`Play`) |

**2. AppHost の更新の制御(E5a)**

- `AppHost` に `runMode_` を持たせ、`GetRunMode()` と `SetRunMode(RunMode)` を足す。`SetRunMode` は**フレームの先頭(`OnBeginFrame` の中)でだけ呼ぶ**(5.3 の「モードはフレームの途中で変えない」)。
- `AppHost::Update` の分岐:

| 処理 | Edit | Play | Pause |
|---|---|---|---|
| `EntityContext::Update(runMode_)` | 選択 | 全部 | 選択 |
| `LevelManager::Tick`、`ResourceManager::Update`、`LinkService::Tick` | ○ | ○ | ○ |
| `module_->OnUpdate()` | × | ○ | × |
| UI の入力と `Screens().Update` | × | ○ | × |
| UI の `CollectRenderItems` | ○ | ○ | ○ |
| `CameraManager::UpdateAll` | ○ | ○ | ○ |
| `module_->OnPreRender()` | × | ○ | ○ |
| `OnImGuiRender`、`OnDebugRenderMenu`、`OnDebugRender` | ○ | ○ | ○ |

  - `OnPreRender` を Pause で呼ぶのは、ゲームが足すオフスクリーンの描画(ミニマップ)を止まった画面にも出すため。Edit ではゲームが始まっていないので呼ばない。
  - ImGui のフックはデバッグパネルのため全モードで呼ぶ。**モジュールは ImGui のフックで Play 中にしか無い状態に触れないこと**(GameModule は今これらを実装していない)。
- `IAppModule` に `OnPlayBegin()` / `OnPlayEnd()` を足す(8.4)。
- **ランタイムでは** `AppHost::Initialize` の中で `module_->OnInitialize()` の直後に `module_->OnPlayBegin()` を 1 回呼び(3. の「ランタイムでの順序」)、`runMode_` は常に `Play`。`OnPlayEnd` は呼ばない(終了時の後始末は今どおり `OnFinalize`)。
- E5a では Editor もランタイムと同じに動かす(`Play` で起動し `OnPlayBegin` を呼ぶ)。E5b で Edit 起動に変える。

**3. GameModule の開始処理の移動(E5a)**

| 処理 | 置き場所 | 理由 |
|---|---|---|
| ミニマップのパイプライン、メインのパイプライン、`GameInput::Initialize`、BGM の `OpenStream` | `OnInitialize` のまま | 起動時の資産。停止しても作り直さない |
| UI フォント・白テクスチャ・決定 SE の先読み(今は `GameFlow::Initialize` の先頭) | `OnInitialize` の**今と同じ位置**(`GameFlow::Preload()` として切り出す静的関数) | 起動時の資産。非同期ロードの開始を遅らせないため(Boot の待ち時間を変えない) |
| `GameFlow::Create` + `Initialize`(先読みを除く: セッションのエンティティ、画面の登録、`BootState`) | `OnPlayBegin` | ゲームの状態機械とセッションのエンティティは再生ごとに作る。画面の登録は同名の上書きなので、繰り返しても害はない |
| BGM の再生(`Play(LoopRegion)`) | `OnPlayBegin` | 再生ごとに頭から鳴らす |

`OnPlayEnd`(E5b で使う。E5a でも書いておく)は次を行う。

1. `GameFlow::Release()`(セッションの破棄の予約。ステージのエンティティと Level はエンジンが片付ける。4. 参照)。
2. `bgmStream_->Stop()`。
3. `minimapBakeRequested_ = false`。
4. パッドのトリガー抵抗を外し(`GameInput::SetTriggerResistance(RTrigger, 0, 0)`)、振動を止める(`StopVibration`)。
5. `OnUpdate` が毎フレーム書く描画の設定を既定値へ戻す。影の中心(`ShadowRenderer::SetSceneCenter`)は `OnInitialize` がプリセットに入れる値(今は `(0, 3, 0)`。`GameModule` の定数にまとめて両方から使う)、モーションブラーの強さ(`MotionBlurEffect::SetStrength`)は 0(既定値)。強さが 0 なので、カメラを戻した直後の行列の飛びでブラーは出ない。

**ランタイムでの順序**: `AppHost::Initialize` の中で、`module_->OnInitialize()` の**直後**(`Register` より前)に `module_->OnPlayBegin()` を呼ぶ。今の `OnInitialize` との差は、`GameFlow` の生成と BGM の再生が、パイプラインの構築と BGM の `OpenStream` の後に動くことだけになる。

- 先読みは元の位置に残すので、非同期ロードの開始は変わらない。
- `GameFlow::Initialize` の残りは、パイプラインに依存しない(セッションのエンティティの即時生成、画面の登録、`BootState::OnEnter`)。
- BGM は今も `OnInitialize` の終わり近く(パイプラインの構築の後)で鳴らしているので、開始の時点はほぼ同じ(デバッグパネルの登録の分だけ前後する)。
- セッションのエンティティは、今と同じく `FinalizeRegistration` の前に作られる。

**4. Editor の遷移(E5b)**

遷移は Editor プロジェクトの `PlayModeController`(新規。`Editor/PlayModeController.h/.cpp`)が行う。ツールバーの操作は要求として記録し、**次のフレームの `OnBeginFrame` の先頭で**実行する。ここは `FlushRender` の後で、ECS の反復の外。レンダースレッドは止まっているが GPU は前フレームを実行中のことがあるので、破棄の前に `WaitForRenderIdle` を呼ぶ。

**Edit のワールドは「Level の組」として記録し、再生のたびに全部作り直す**(7.1 の「編集モデルが正本、ワールドはプレビュー」と同じ考え方)。再生の前からあるエンティティを残すと、Play の System(アニメーション、ゲームの System など)が書き換えた値・コンポーネント・親子関係が残り、停止後に一致しないため。

再生(Edit → Play):

1. `WaitForRenderIdle()` → `FlushPendingCommands()`。
2. 編集中の Level のプレビューをアンロードする(`LevelEditModel::ClearPreview()`、5. 参照)。プレビューがあったかを覚える。
3. 「Edit のワールド」を記録する。ロード済みのルート Level の出どころ(パスか in-memory 登録のキー。`LevelManager::GetSourceKey`、5. 参照)、`Main` と `Offscreen` のカメラの状態。
4. ワールドを空にする(下の「ワールドを空にする」)。
5. `SetRunMode(Play)` → `module.OnPlayBegin()`。

同じフレームの `EntityContext::Update` からゲームが動く。ランタイムで `OnPlayBegin` の後に最初のフレームが来るのと同じ。8.1 の「開始待ち」は要らなくなった(再生時にプレビューを生成しないため)。

一時停止 / 再開:

- 一時停止: `SetRunMode(Pause)` → `SoundEngine::PauseAll()`。**Pause の間は `Engine` が `SoundEngine::Update` と `AudioDirector::Update` を呼ばない**(サウンドの時間を止める)。`AudioDirector` は一時停止したストリームを `IsPlaying() == false` から「終わった」と見なして回収してしまい、`SoundEngine::Update` はフェードを進めて、フェードアウト中の音を止めて回収したり、ストリームを先頭へ戻したりするため。ストリームの供給(`Pump`)も止まるが、一時停止中のボイスは消費しないので途切れない。
- 再開: `SoundEngine::ResumeAll()` → `SetRunMode(Play)`。

停止(Play / Pause → Edit):

1. Pause からなら `SoundEngine::ResumeAll()`。続けて `module.OnPlayEnd()` → `AudioDirector::StopAll()`(仮想化されたインスタンスも含めて全部止めて回収する。5. 参照)→ `SoundEngine::StopAll()`(`AudioDirector` を通さない SE の残りも止める)。
2. `WaitForRenderIdle()` → `FlushPendingCommands()`。
3. ワールドを空にする。
4. `UIScreenManager::Clear()`(5. 参照)と `UIInputSystem::ClearState()`。
5. カメラの状態を戻す。
6. 記録した Level を記録の順に `LevelManager::Load` で読み直す(生成は次の Flush。サブ Level の `loadOnStart` も通常のロードと同じく展開される)。
7. プレビューがあったなら `LevelEditModel::RequestPreview()` → `ProcessPreviewRequest()` を**ここで直接呼ぶ**。Level エディタの `DebugRender` は、ゲーム用のデバッグ UI を隠す(F1 など)と呼ばれないので任せない。
8. `SetRunMode(Edit)`。

ワールドを空にする(再生の 4 と停止の 3):

1. `LevelManager::UnloadAll()`(子 Level と非同期ロードの取り消しも含む。E4b)→ `FlushPendingCommands()`。
2. 残っている全エンティティ(Level を使わずに作ったもの。ゲームのステージの地形・プレイヤー・コインなど)を `RequestDestroyEntity`(`IsValid` のものだけ)→ `FlushPendingCommands()`。全エンティティの列挙は `EntityContext::CollectAllHandles`(5. 参照)。

- 呼ぶ前に `WaitForRenderIdle` 済みで、間に描画を挟まないので、GPU の完了待ちは 1 回でよい(7.3 と同じ考え方)。
- **Edit で Level を使わずに作ったエンティティは、再生で消えて戻らない**(SceneHierarchy のデバッグ操作で足したものなど)。Edit の正本は編集モデルと Level なので、これは仕様とする。Editor は Edit で起動し、GameModule の `OnInitialize` はエンティティを作らない(セッションは `OnPlayBegin` へ移す)ので、起動直後の Edit のワールドは空になる。
- **再生中は Level エディタを操作できなくする。** ウィンドウ全体を無効表示にし、New / Load / Save / ノード編集 / Load in World / Load Async / Reload Refs を受け付けない(`AppHost::GetRunMode() != Edit` のとき)。8.1 の「編集モデルは Play 中に変更しない」を守るためと、ワールドへの操作が記録と食い違うため。

**5. エンジンに足す補助(E5b)**

| 追加 | 内容 |
|---|---|
| `EntityContext::CollectAllHandles(std::vector<EntityHandle>&)` | 型条件が空の View(`GetView<>()`)で全エンティティのハンドルを集める。実装では `Archetype::Create<>()` が空の型列でコンパイルできなかったので、空のときは `CreateImpl` を呼ばないようにした(E5b)。`CreateEntityFromTypes` で作ったエンティティは `TransformComponent` を持たないことがあるので、Transform の View は使わない。反復と Flush の外でだけ呼ぶ |
| `LevelManager::CollectLoadedRoots(std::vector<LevelId>&)`、`GetSourceKey(LevelId)` | ロード済みで親を持たない Level を集める(`UnloadAll` の収集部分を切り出す)。`GetSourceKey` はその Level をロードしたときのパスか in-memory 登録のキー(スロットが持つ `path`)を返す |
| `LevelEditModel::ClearPreview()` | 7.3 の 1〜5(完了待ち、Flush、アンロード、Flush、登録の解除)だけを行い、新しいプレビューは作らない。プレビューがあったら true |
| `LevelEditorPanel::GetModel()`、`AppHost::GetLevelEditModel()` | Editor が Level エディタの編集モデルを使うための口(`AQ_DEBUG_IMGUI`) |
| `UIScreenManager::Clear()` | スタックの全画面を上から `OnExit` → `OnDestroy` の順で即時に破棄する。保留中の操作、Click の待ち、Exit の待ちも捨てる。画面の登録は残す |
| `Camera::State`、`CaptureState()` / `RestoreState()` | 位置・注視点・Up・Near・Far・画角・正射影(有無と幅・高さ)。アスペクト比は含めない(停止中にゲームビューの大きさが変わっていることがあるため。E3b) |
| `AudioDirector::StopAll()` | 全インスタンス(仮想化されたものを含む)を止めて回収する。`StopAllByKind` の全種類版 |
| `Engine` の `SoundEngine::Update` と `AudioDirector::Update` の呼び出し | `AppHost::GetRunMode() == Pause` のときは呼ばない |
| `SoundEngine::GetActiveVoiceCount()` | 再生中のボイス数。自己テストの比較に使う。ワンショットのアクティブ数 + 3D 発音体の再生中の数 + 再生中のストリームの数(E5b で決定) |

**6. 自己テスト(E5b)**

Editor のメニューに「Play/Stop 自己テスト」を置く(Editor プロジェクト。エンジンには足さない)。押すと、再生 → 120 フレーム待つ → 停止 → **復元の完了を待つ** → 指紋を取る、を 5 回繰り返し、毎回の指紋を再生前の指紋と比べて、結果を `startup_timing.log` に出す。比べるものは 11.3 の E5 の項目と同じ。

- **復元の完了**: 記録した全 Level とプレビューが `IsSpawned` になった後、さらに 2 フレーム(生成の Flush の後の階層変換と描画データの更新を待つ)。300 フレーム経っても揃わなければ不一致として記録する。
- 再生前の指紋も、同じ条件(Edit で全 Level が `IsSpawned` になって 2 フレーム後)で取る。
- リソース(モデル・テクスチャ)の読み込みの完了は指紋に含めない。見た目はユーザー確認で見る。

| 指紋の項目 | 取り方 |
|---|---|
| 編集モデル | `LevelEditModel::ToJson()` を文字列にしたもの |
| エンティティ | 親子の木の構造を名前に頼らずに文字列にする。各エンティティの文字列 = そのエンティティの全コンポーネントの JSON(`ComponentRegistry::GetAll()` を回して `meta.has(handle)` のものを `meta.serialize(handle, ...)`。インスペクターと同じ列挙で、Animation などの専用保存も含む)+ 子の文字列を並べ替えて連結したもの。ルート(親の無いエンティティ)の文字列を並べ替えて連結する。名前は空・重複・切り詰めがあるので識別に使わない |
| Level | プレビュー以外のロード済みのルート Level の出どころ(並べ替えて連結)と、プレビューがあるか。プレビューは置き換えのたびに登録キーが変わる(`<level-editor-preview-N>`)ので、キーは比べない |
| UI | `StackSize()` |
| サウンド | `GetActiveVoiceCount()` |

- `serialize` に出ない実行時の状態(アニメーションの再生時刻など)は指紋では比べられない。停止のたびにエンティティを作り直すので、これらは生成直後の値になる。
- ツールバーはエディタ用コンテキストのメインメニューバーに置いた(Play / Pause・Resume / Stop / モード表示 / Self Test)。自己テストは起動引数 `-editor-selftest` でも始められ、終わると終了コード(PASS は 0、FAIL は 3)で Editor を閉じる。準備として `Assets/Levels/Playground.level.json` をロードし、Level エディタのプレビューを作る。
- ゲームはタイトルで入力を待つので、自己テストの再生はタイトルまでしか進まない。ステージの読み込み中・プレイ中の停止は、ユーザー確認で行う(この PC では自動操作のキーが DirectInput に届かない)。

**7. 採らなかった案**

- **停止時に個別の状態を 1 つずつ戻す**: 戻すものが多く(上の調査の表)、ゲームが増えるたびに漏れる。`GameFlow` を作り直し、ワールドは Level から作り直す。
- **再生前からあるエンティティを残し、再生中に増えた分だけ消す**(Codex レビュー 1 回目で取り下げ): 残したエンティティを Play の System が書き換えるので、停止後に元へ戻らない。子 Level や非同期ロードの取り消しも漏れる。
- **ECS 全体のバイナリスナップショット**: 8.1 のとおり作らない。
- **Pause でゲームの `OnUpdate` を呼んで、ゲーム側で止める**: ゲームごとに止め方を書くことになる。

### テスト方針

| 段 | 自動で確認する | ユーザーに確認する |
|---|---|---|
| E5a | ビルド(Game の D3D11 / D3D12 × Debug / Release、Editor の 2 構成)と警告の増加なし。ReflectionTest に T9(モードによる System の選択と、全モードでの Flush)を足し、T1〜T9 が全部 PASS。Game.exe と Editor.exe が起動して終了コード 0 | Game.exe でタイトル → ステージ → リザルト → タイトルが今までどおり動く(BGM を含む) |
| E5b | ビルド。Editor.exe が Edit で起動する(ログでモードを確認)。Claude の自動操作で再生・一時停止・再開・停止のボタンを押し、クラッシュとエラーのログが無い。自己テストが 5 回とも一致 | 停止後の見た目が再生前と同じ。ステージの読み込み中とプレイ中に停止してもクラッシュせず元に戻る。停止で BGM が止まり、再生で頭から鳴る。Game.exe の挙動が変わらない |

---

## 評価チェックリスト(旧 11.3 節)

### E5 Edit / Play / Pause

詳細は 8.5 節。E5a と E5b に分ける。

E5a(エンジンの口):

- [x] ビルド 6 構成(Game の D3D11 / D3D12 × Debug / Release、Editor_Debug / Editor_Release)が通り、警告が増えない(重複を除いて 17 件のまま)
- [x] ReflectionTest の T9: `Edit` / `Pause` で宣言の無い System が呼ばれず、宣言のある System は呼ばれる。全モードで予約したコマンドが Flush される。T1〜T9 が全部 PASS(CMake windows-vs2026 で Debug 158/158、Release 116/116)
- [x] Game.exe と Editor.exe が起動し、終了コード 0、`[crash]` が無い(Game の D3D11 / D3D12 × Debug / Release と Editor の 2 構成。12 秒動かして `CloseMainWindow`。ログで `OnPlayBegin ok` を通ることも確認)
- [x] Game.exe でタイトル → ステージ → リザルト → タイトルが E5a 前と同じに動く(BGM を含む。2026-11-20 ユーザー確認、D3D12 Debug)

E5b(Editor の再生・停止):

- [x] Editor.exe が Edit で起動し、ゲームの更新と UI が止まっている(GameFlow が作られていない。撮影でワールドが空・UI 無しを確認)
- [x] Claude の自動操作で再生・一時停止・再開・停止を押し、クラッシュとエラーのログが無い(撮影でタイトル表示・停止中の静止・停止後の空のワールドを確認。再生中にウィンドウを閉じても終了コード 0)
- [x] 自己テスト(8.5 の 6.)が 5 回とも一致する(`-editor-selftest`、Editor_Debug / Editor_Release とも PASS・終了コード 0)
- [x] 再生中は Level エディタ全体が操作できない(New / Load / Save / ノード編集 / Load in World / Load Async / Reload Refs。ユーザー確認)
- [x] ゲーム用のデバッグ UI を隠した(F1)状態で停止しても、プレビューが戻る(ユーザー確認)
- [x] Pause → 再開で BGM が続きから鳴る(Pause 中に `SoundEngine::Update` / `AudioDirector::Update` が呼ばれない。ユーザー確認)

元のチェックリスト(E5b で確認する):

- [x] Edit 中は GameModule の更新と UI が止まり、8.2 の表の System だけが動く。Pause 中はパーティクルも止まる(System の選択は T9、画面は撮影とユーザー確認)
- [x] Edit / Pause 中にも遅延コマンドが反映される(プレビュー置換がどのモードでも完了する。T9 の Flush と自己テストのプレビュー復元)
- [x] 再生 → 停止を 5 回繰り返しても、停止後のワールドが再生前と一致する(自己テスト。親子は名前でなく木の構造で比べる。8.5 の 6.)。比べるもの: 編集モデルを保存した JSON(再生前と停止後で同一)、エンティティ数、親子関係、`ComponentMeta::serialize` を持つ全コンポーネント(Reflect 版と専用版の両方。例: Animation の専用保存)を `serialize` した JSON、BGM の再生数、UI Screen の数
- [x] 停止後に、モデルなどのリソースが読み込み完了の状態で、読み込み失敗のログが出ていない。ゲームビューに再生前と同じ見た目で描かれる(ユーザー確認。ログに失敗・エラー無し)
- [x] ステージのロード中に停止しても、クラッシュせず停止後のワールドが再生前と一致する(ユーザー確認)
- [x] Game.exe の挙動が E5 前と同じ(ランタイムは常に Play。E5a でユーザー確認、E5b 後も起動・終了コード 0)
- [x] 8.3 の TODO(静的変数の洗い出し)を埋めた
