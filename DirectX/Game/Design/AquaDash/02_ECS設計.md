# 02. ECS 設計

> 対象コミット: e796de8 / 最終更新: 2026-09-07
> 対応要求: R-05, R-07〜R-11, R-13, R-15([00_企画概要.md](00_企画概要.md))

ゲーム固有の Component / System を定義する。配置は `Game/Application/ECS/`(既存 Actor 系と同居)、
名前空間は `app::ecs`。既存の `ActorComponentSystem` / `CameraSteeringComponentSystem` の
流儀(Component にデータ、System に処理、`aq::ecs::Foreach<T>`)に合わせる。

## 1. コース追従モデル(移動と重力の中核)

ソニック的な走行(壁走り・ループ)を汎用物理でやると発散するため、
**コースをスプラインで定義し、キャラはスプライン座標系で動かす**方式を採る。

- ステージはスプライン(中心線 + 各点の up ベクトル + 路面幅)の列
  ([03_ステージデータ仕様.md](03_ステージデータ仕様.md))。
- キャラの位置は `(distance, lateral, height)` のスプラインローカル座標で持ち、
  ワールド変換はスプライン評価で得る。**重力はスプラインの -up 方向**なので、
  ループでは up が 1 回転し、自然に「重力方向が変わる」(R-13)。
- 左スティック: 縦 = 加減速(前後)、横 = レーン移動(lateral)。A = ジャンプ(height に初速)。
  進行方向はコースが決めるため、カメラ自動化(R-09)とも整合する。
- ジャンプ中に路面から大きく外れて `height` が下限(落下しきい値)を割ったら落下扱い(R-05)。

## 2. Component 責務表

| Component | 主データ | 用途 |
|---|---|---|
| `SpeedCharacterComponent` | distance / lateral / height、速度、接地フラグ、状態(走行/ジャンプ/落下/ゴール済) | プレイヤー 1 人分の走行状態 |
| `PlayerInputComponent` | 移動入力、ジャンプ入力 | 既存 `GameInput`(キーボード+パッド0)から書き込む(R-07 変更で一人プレイ専用) |
| `CoinComponent` | 取得済みフラグ、回転位相 | コイン 1 枚。位置は Transform が持つ |
| `GoalComponent` | 発火済みフラグ | ゴールトリガー |
| `PlayerScoreComponent` | コイン枚数、落下回数 | リザルト評価の集計元(R-11) |
| `AutoCameraComponent` | 追従対象 EntityHandle、モード(追走/リザルト周回)、現在位置・注視点(平滑化用) | プレイヤー毎の自動カメラ(R-09)。ビュー番号を持ち分割画面と対応 |
| `SessionComponent`(P16) | gameplayPaused / activeStage / playerHandle / collectFxHandle / coinInstancesHandle / ミニマップ正規化パラメータ | **セッションエンティティ(1体)に載せる共有進行状態**。書き込みはメインスレッド(GameFlow の状態クラス)のみ、System はワーカーから読み取り専用。旧 `GameContext`(GameFlow 保持のサービスロケータ)を置換 → §6 |

- コインとゴールの判定はスプライン座標で行うため、コイン側にも
  ロード時に `(distance, lateral, height)` を焼いておく(毎フレームの逆変換を避ける)。

## 3. System 責務表(実行順)

| System | 処理 | 実行条件 |
|---|---|---|
| `PlayerInputSystem` | `GameInput`(パッド N 台)→ `PlayerInputComponent` へ転写 | 常時 |
| `SpeedCharacterSystem` | 入力から加減速・レーン移動・ジャンプ・重力を積分し、スプライン評価で Transform(位置 + 姿勢)を書き出す | Pause 中は停止 |
| `CoinSystem`(実装名。旧称 CoinCollectSystem) | コインの回転演出 + プレイヤーとの距離判定(スプライン座標で distance を粗く絞ってから 3D 距離)。取得でスコア加算 + 取得エフェクト/SE。描画は P10 でインスタンス化: 毎フレーム非取得コインだけを Coins エンティティの `InstancePoints` へ再構築する(取得=リストから消える、`ReactivateAll`=全数再構築で復活。破棄はしない) | Pause 中は停止 |
| `GoalFallJudgeSystem` | ゴール distance 通過判定 / 落下しきい値判定。結果を `GameContext.playResult` へ | Pause 中は停止 |
| `AutoCameraSystem` | モード別にカメラ位置を平滑追従(後述)。ビュー行列を分割画面ビューへ出力 | 常時(Result 周回もここ) |

- 構造変更(コイン取得時の破棄など)は行わず表示フラグで済ませる。必要になった場合も
  規約どおり `Request*` 遅延コマンドに積む(architecture.md §5)。
- `SpeedCharacterSystem` の 4 人分の積分は独立なので、プレイヤー数分を
  ThreadPool で並列化できる形(書き込み先が自分の Component のみ)に保つ(R-16)。

## 4. 自動カメラ(R-09)

- 追走モード: スプライン上の「プレイヤーの少し後方 distance」を評価した点 + up 方向オフセットに
  カメラを置き、プレイヤーの少し前方を注視。位置・注視点とも指数平滑。
  ループ中も up がスプライン準拠なので画面が自然にロールする。
- 速度に応じて FOV を広げる(基準 60°→ 最高速 75° 程度)。スピード感の主演出のひとつ(R-10)。
- リザルト周回モード: 終了地点を中心に低速で円軌道 + 注視固定。
- 既存 `CameraSteeringComponentSystem` は流用せず新設(スプライン前提のため)。
  共通化できる平滑化ユーティリティがあれば移す。

## 5. スピード感の数値設計(R-10, R-12)

| 項目 | 値(初期案) |
|---|---|
| 最高速 | 83m/s(≒ 300km/h) |
| 通常巡航 | 40〜55m/s |
| ステージ長 | 約 8,000m(巡航平均 45m/s × 180 秒) |
| キャラスケール | unityChan 系 FBX は約 60 倍スケール要(既知)。ステージ側の単位は 1m = 1.0 で統一し、モデル側で吸収 |

- 数値は `.stage.json` とキャラ定義に置き、コードに埋め込まない。

## 6. セッション状態の ECS 化(P16: SessionComponent)

> 設計 2026-09-09。挙動を変えない構造リファクタ。発端は 2026-09-06 の設計メモ
> 「System からの `GameFlow::Get().Context()` 参照はサービスロケータ的で依存が見えない。
> 読み取り専用はただの契約」(README 既知の課題)。加えて「GameFlow が状態の入れ物を
> 兼ねるのも不自然」という指摘(2026-09-09)を反映し、**状態の持ち主を ECS ワールドにする**。

### 6.1 分割方針

旧 `GameContext` のフィールドを 2 つに分ける:

| 行き先 | フィールド | 理由 |
|---|---|---|
| **`SessionComponent`**(ECS。セッションエンティティ 1 体) | `gameplayPaused` / `activeStage` / `playerHandle` / `collectFxHandle` / `coinInstancesHandle` / `minimapCenterXZ` / `minimapHalfExtent` | System・UI が読む共有状態 |
| **GameFlow の私有メンバ** | `selectedStageIndex` / `playResult` / `stageList` / `stageEntities` | 状態機械の進行にしか使わない(System は読まない) |

`GameContext.h` は廃止(PlayResult 等の型は GameFlow 側へ)。`GameFlow::Context()` も削除する。

### 6.2 アクセス規約

- セッションエンティティは `GameFlow::Initialize` で生成し、ステージ再入場でも破棄しない
  (`stageEntities` に積まない)。`Finalize` で破棄。
- **書き込みはメインスレッドのみ**(GameFlow の状態クラスと `Application::OnUpdate`
  ではなく状態クラスに限定。OnUpdate は読み取りのみ)。
- **System はワーカーから読み取り専用**: エンジンに追加する
  `aq::ecs::EntityContext::GetSingletonComponent<T>()`(T を持つ唯一のエンティティを
  走査して返す。無ければ nullptr)を **const ポインタで受ける**。
- 実行順の安全性は従来と同じ「EntityContext::Update(ワーカー)完了後に
  GameFlow::Update(メイン)が書く」に依る。本リファクタで変わるのは所有と経路の明示で、
  スケジューラへの自動依存導出は将来課題(エンジンの依存宣言は AddSystem の明示のまま)。

### 6.3 影響範囲

- エンジン: `EntityContext::GetSingletonComponent<T>()` の追加のみ。
- ゲーム: `SessionComponent`(`Application/ECS/SessionComponent.h` 新規)、
  CoinSystem / SpeedCharacterSystem / AutoCameraSystem / PlayerInputSystem の参照置換
  (System から `GameFlow.h` の include が消える)、状態クラスと Application.cpp の書き換え。

## チェックポイント

- [ ] スプライン走行で直線・カーブ・ループ(重力反転)を走り切れる
- [ ] 4 パッドがそれぞれ別キャラを動かせる
- [ ] コイン取得がスコアに反映され、「もう一度」で全コインが復活する
- [ ] ゴール通過と落下がそれぞれ正しく Result へ通知される
- [ ] Pause タグでゲーム System だけが止まり、描画系は動き続ける
