#pragma once


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
			PlayResult playResult;
		};
	}
}
