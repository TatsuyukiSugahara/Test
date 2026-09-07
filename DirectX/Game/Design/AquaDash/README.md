# AquaDash(仮称)— ソニックライク エンジン動作検証ゲーム

> 対象コミット: e796de8 / 最終更新: 2026-09-07

エンジン(aqEngine)の総合動作確認を目的とした、ハイスピード 3D アクションゲーム。
AI 駆動開発の題材として「設計 → 実装 → 評価」のサイクル
(スキル `feature-dev-cycle` 参照)で進める。

## ドキュメント構成

| ファイル | 内容 |
|---|---|
| [00_企画概要.md](00_企画概要.md) | 要求仕様(一次資料)・受け入れ条件 |
| [01_シーンフロー設計.md](01_シーンフロー設計.md) | タイトル / インゲーム / リザルトの状態遷移と非同期ロード |
| [02_ECS設計.md](02_ECS設計.md) | Component / System 一覧、移動・重力・カメラ・判定の設計 |
| [03_ステージデータ仕様.md](03_ステージデータ仕様.md) | .stage.json フォーマット(コース / コイン / ランク条件) |
| [04_描画_分割画面_UI設計.md](04_描画_分割画面_UI設計.md) | 4 人分割画面、モーションブラー、HUD / ミニマップ |
| [05_実装フェーズ計画.md](05_実装フェーズ計画.md) | フェーズ分割と各フェーズの評価チェックリスト |

## フォルダ構成

AquaDash 専用フォルダは作らず、**Game 直下の既存構成に統合**する
(エンジンだけ流用する際は Game フォルダごと差し替える方針のため、Game 内での二重の名前空間は不要)。

```
Game/
├── Design/AquaDash/          … 本設計書一式(このフォルダ)
├── Application/
│   ├── Flow/                 … GameContext / TitleState / InGameState / ResultState 等
│   ├── ECS/                  … SpeedCharacter / Coin / Goal / AutoCamera 等(既存 Actor 系と同居)
│   ├── Stage/                … ステージデータ(スプライン / StageRegistry)
│   └── UI/                   … 画面クラス(タイトル / HUD / リザルト)
└── Assets/
    ├── Stages/               … *.stage.json + 参照する *.level.json
    └── UI/AquaDash/          … 画面レイアウト JSON(既存 Title.screen.json との名前衝突回避のためサブフォルダ維持)
```

- エンジン側に手を入れる項目(分割画面ビュー、モーションブラー、ゲームパッド 4 台)は
  各設計書に「エンジン側作業」として明記し、`DirectX/設計書/` の該当設計書と重複記述しない。
- 設計書の運用ルール(対象コミット表記・責務表・チェックポイント)は
  `.claude/skills/cpp-gamedev/references/architecture.md` §6 に従う。

## 既知の課題

- **[設計メモ 2026-09-06] System からの `GameFlow::Get().Context()` 参照はサービスロケータ的で ECS の依存管理から見えない。**
  現状は「EntityContext::Update(ワーカー並列)完了後に GameFlow::Update(メイン)」の順序と
  「System は Context を読み取り専用」という契約(GameContext.h に明記)で安全だが、コードで強制されない。
  あるべき形はシングルトンエンティティ+`SessionComponent` 化(共有状態を ECS に載せ、依存をスケジューラに見せる)。
  リファクタは保留中 — 着手時は CoinSystem / SpeedCharacterSystem / AutoCameraSystem / 状態クラスが対象。

- 旧「残刃」フロー(GameFlow.cpp 内の TitleState / LoadingState / PlayingState と Title 画面)が
  未接続のまま残存。BootState も残刃用 CorporateLogo フォントの準備完了を待ち続けている。
  残すか削除するか P7 で判断する。
- 新規アセット(`Assets/UI/AquaDash/*.screen.json` 等)の UWP パッケージ登録は未対応。
  動作確認はデスクトップ(Win32)のみ。
- P0 の「NEXT STAGE」は同一ステージの再プレイ(StageRegistry 導入後に次ステージへ)。
- エンジンの `Camera` に up ベクトル指定 API が無く、ループ中のカメラロールは不可。
  P4 で `SetUp` をエンジン側へ追加してから対応する(AutoCameraComponentSystem.cpp 参照)。
- 走行アニメが無く idle 固定(unityChan の走りモーション未導入)。スピード感演出の一部として P6 で検討。
- 路面の見た目が無い(平坦地形の上を見えないスプラインで走る P1 最小構成)。路面メッシュ生成は後続フェーズで検討。
- unityChan.tkm はメートル基準でないため `PLAYER_MODEL_SCALE = 0.25` で縮小している。
- コイン/路面タイルの見た目は仮(箱に見立てたボックス。専用モデルは今後の課題)。
  取得 SE も Decision.wav 流用の仮。専用アセット導入時に `CoinComponentSystem.cpp` のパスを差し替える。
  (P8 で `BoxStaticMeshComponent::SetColor` を追加し赤単色は解消: 路面=青みグレー/コイン=ゴールド。
   ただし色の `Reflect` 登録は反射 Visitor に `Vector4` 対応が無く見送り — 箱色はコード設定専用で
   Prefab JSON へ永続化できない)
- (P8〜P10 で改善) 箱は `StaticMesh::SetLocalBounds` の明示 AABB でフラスタムカリング対象になり
  (カリング設計.md §13)、P9 で路面タイル・P10 でコインを 1 ドローのインスタンス描画 +
  per-instance フラスタムカリングへ移行(同 §14。コインは CoinSystem が毎フレーム
  未取得分だけ InstancePoints を再構築)。開始地点 66→97→**133fps**・リザルト 322fps。
  **180fps(5.56ms)には未達** — 残り約 7.5ms は地形/キャラ/ディファード/シャドウ/UI 等の
  ベースコストで、コース密度由来のボトルネックは解消済み。以降はポストプロセスチェーン化など
  一般最適化の領域。
- 丘の前後で路面タイルが見えない区間がある: 制御点 y=0 の隣に丘頂点(y=18〜28)を置いた
  Catmull-Rom のアンダーシュートでスプラインが最大約 2m 沈み、タイル(上面 = スプライン +0.05m)が
  平坦地形(y=0)に埋まるため。P7 以前からの既存現象(P8 の実機確認で顕在化を確認)。
  対策案: 丘の前後に y=0 の制御点を追加してアンダーシュートを抑える、またはタイルを地形より浮かせる。
  (P9 設計時の机上検証: ショルダー制御点を足しても沈みは -0.26m 程度までしか抑えられず、
   タイル上面 +0.05m では埋まりが残る。完全解決には地形を下げるかスプラインの接線制御が必要 → 保留)
- 残刃用オフスクリーンパスは停止済み(未使用+シーン二重描画のコスト。Application::OnPreRender 冒頭 return)。
- (解決済) エンジンの `Quaternion::SetRotation` 代入バグは修正し、ゲーム側の回避実装も削除した。
