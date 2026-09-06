# AquaDash(仮称)— ソニックライク エンジン動作検証ゲーム

> 対象コミット: ccfce22 / 最終更新: 2026-09-06

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

## 予定フォルダ構成(実装開始時に作成)

```
Game/
├── Design/AquaDash/          … 本設計書一式(このフォルダ)
├── Application/
│   └── AquaDash/             … ゲーム固有コード(状態 / Component / System)
│       ├── Flow/             … TitleState / InGameState / ResultState 等
│       ├── ECS/              … SpeedCharacter / Coin / Goal / SplitCamera 等
│       └── UI/               … HUD / ミニマップ / リザルト画面
└── Assets/
    ├── Stages/               … *.stage.json + 参照する *.level.json
    └── UI/AquaDash/          … タイトル / HUD 用テクスチャ
```

- エンジン側に手を入れる項目(分割画面ビュー、モーションブラー、ゲームパッド 4 台)は
  各設計書に「エンジン側作業」として明記し、`DirectX/設計書/` の該当設計書と重複記述しない。
- 設計書の運用ルール(対象コミット表記・責務表・チェックポイント)は
  `.claude/skills/cpp-gamedev/references/architecture.md` §6 に従う。
