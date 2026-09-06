#pragma once
#include "ECS/Entity.h"
#include "Stage/StageData.h"


namespace app
{
	namespace aquadash
	{
		/** 最大プレイヤー数 (パッド 4 台・4 分割画面) */
		static constexpr uint32_t MAX_PLAYER_COUNT = 4;


		/**
		 * 1 プレイの結果。InGame が書き込み、Result が評価に使う
		 */
		struct PlayResult
		{
			bool     cleared      = false;                // true=クリア / false=ゲームオーバー (落下)
			float    clearTimeSec = 0.0f;                 // ゴール (または終了) までの経過秒
			uint32_t coinCounts[MAX_PLAYER_COUNT] = {};   // プレイヤー毎の獲得コイン枚数
		};


		/**
		 * 状態間で共有するゲーム進行データ (GameFlow が保持)
		 */
		struct GameContext
		{
			int        selectedStageIndex = 0;   // タイトルで選んだステージ (StageList の並び順)
			uint32_t   joinedPadMask      = 1;   // 参加プレイヤーのパッド番号ビットマスク
			uint32_t   playerCount        = 1;   // 参加人数 (P1 は 1 固定。P5 で複数化)
			PlayResult playResult;

			/** ステージ */
			std::vector<stage::StageListEntry> stageList;     // タイトルで読む一覧
			std::shared_ptr<stage::StageData>  activeStage;   // ロード済みステージ定義 (不変)

			/** ワールド */
			bool  gameplayPaused = false;   // true でゲーム System (走行/判定) を停止 (リザルト用)
			aq::ecs::EntityHandle              playerHandles[MAX_PLAYER_COUNT];
			aq::ecs::EntityHandle              collectFxHandles[MAX_PLAYER_COUNT];   // コイン取得エフェクトの常駐エミッタ
			std::vector<aq::ecs::EntityHandle> stageEntities;   // タイトル復帰時に破棄する生成物
		};
	}
}
