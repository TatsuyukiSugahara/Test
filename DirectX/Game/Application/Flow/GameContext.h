#pragma once
#include "ECS/Entity.h"
#include "Stage/StageData.h"


namespace app
{
	namespace aquadash
	{
		/**
		 * 1 プレイの結果。InGame が書き込み、Result が評価に使う
		 */
		struct PlayResult
		{
			bool     cleared      = false;   // true=クリア / false=ゲームオーバー (落下)
			float    clearTimeSec = 0.0f;    // ゴール (または終了) までの経過秒
			uint32_t coinCount    = 0;       // 獲得コイン枚数
		};


		/**
		 * 状態間で共有するゲーム進行データ (GameFlow が保持)。
		 * 書き込みは GameFlow::Update (状態クラス) のみが行い、ECS System からは
		 * 読み取り専用で参照する契約 (System はワーカースレッドで並列実行されるため)。
		 */
		struct GameContext
		{
			int        selectedStageIndex = 0;   // タイトルで選んだステージ (StageList の並び順)
			PlayResult playResult;

			/** ステージ */
			std::vector<stage::StageListEntry> stageList;     // タイトルで読む一覧
			std::shared_ptr<stage::StageData>  activeStage;   // ロード済みステージ定義 (不変)

			/** ワールド */
			bool  gameplayPaused = false;   // true でゲーム System (走行/判定) を停止 (リザルト用)
			aq::ecs::EntityHandle              playerHandle;      // プレイヤー
			aq::ecs::EntityHandle              collectFxHandle;   // コイン取得エフェクトの常駐エミッタ
			std::vector<aq::ecs::EntityHandle> stageEntities;     // タイトル復帰時に破棄する生成物

			/** ミニマップ (コース XZ 範囲 → 0-1 正規化のパラメータ) */
			aq::math::Vector2 minimapCenterXZ;            // コース範囲の中心 (XZ)
			float             minimapHalfExtent = 1.0f;   // 正方形マップに収める半径 [m]
		};
	}
}
