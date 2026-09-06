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
		 * 分割画面のビュー矩形 (ピクセル)。2 人=上下、3-4 人=田の字 (3 人時の右下は空き)
		 */
		inline void GetSplitViewRect(const uint32_t playerCount, const uint32_t index,
		                             const float screenW, const float screenH,
		                             float& outX, float& outY, float& outW, float& outH)
		{
			if (playerCount <= 1) {
				outX = 0.0f; outY = 0.0f; outW = screenW; outH = screenH;
				return;
			}
			if (playerCount == 2) {
				outW = screenW;
				outH = screenH * 0.5f;
				outX = 0.0f;
				outY = index == 0 ? 0.0f : outH;
				return;
			}
			outW = screenW * 0.5f;
			outH = screenH * 0.5f;
			outX = (index % 2 == 0) ? 0.0f : outW;
			outY = (index / 2 == 0) ? 0.0f : outH;
		}


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
			uint32_t   playerCount        = 1;   // 参加人数 (タイトルで確定。分割画面のビュー数)
			bool       inputCloneAll      = false;   // true で全プレイヤーがパッド0/キーボード入力を共有 (パッド無しの分割テスト用)
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
