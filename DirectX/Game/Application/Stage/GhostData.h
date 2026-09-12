#pragma once


namespace app
{
	namespace stage
	{
		/**
		 * ゴーストの 1 サンプル (スプラインローカル座標。20 バイト)。
		 * ワールド座標や行列ではなくスプライン座標で持つ。スプラインを通せば同じ姿勢に
		 * 復元でき、1 サンプルが小さく済むため (設計 05 §P23-1)。
		 */
		struct GhostSample
		{
			float timeSec  = 0.0f;   // ステージ開始からの経過秒 [s]
			float distance = 0.0f;   // スプライン距離 [m]
			float lateral  = 0.0f;   // 路面中心からの横位置 [m]
			float height   = 0.0f;   // 路面相対の高さ [m]
			float roll     = 0.0f;   // エアトリックのロール角 [rad]
		};




		/**
		 * 1 本のゴースト (自己ベスト走行)。
		 * ファイル I/O は CPU 処理だけで完結するのでワーカースレッドから呼んでよい
		 * (ECS / GPU リソース / シングルトンには触れない)。
		 */
		struct GhostData
		{
			/** 走行結果 */
			float                    clearTimeSec = 0.0f;   // この走行のクリアタイム [s]
			std::vector<GhostSample> samples;               // timeSec 昇順。30Hz で記録する


			/**
			 * 経過秒から姿勢を線形補間して返す。
			 * @param timeSec ステージ開始からの経過秒 [s]。範囲外は端をクランプする
			 * @param out     補間結果
			 * @return サンプルが 1 件も無ければ false
			 */
			bool Evaluate(const float timeSec, GhostSample& out) const;

			/**
			 * 指定のスプライン距離へ到達した時刻を引く (HUD の時間差計算用)。
			 * @param distance スプライン距離 [m]。範囲外は端をクランプする
			 * @param outTimeSec 到達時刻 [s]
			 * @return サンプルが 1 件も無ければ false
			 */
			bool TimeAtDistance(const float distance, float& outTimeSec) const;


			/**
			 * 独自バイナリ .ghost を読む (CPU 処理のみ。ワーカースレッドから呼んでよい)。
			 * ファイルが無い / バージョンやステージ ID が食い違う場合も nullptr を返す
			 * (エラーではなく「ゴースト無し」として扱う)。
			 * @param path    .ghost のフルパス
			 * @param stageId 照合するステージ ID。nullptr なら ID の照合を省く
			 * @return 読めたゴースト。失敗時は nullptr
			 */
			static std::shared_ptr<GhostData> LoadFromFile(const char* path, const char* stageId = nullptr);

			/**
			 * 独自バイナリ .ghost を書く (CPU 処理のみ。ワーカースレッドから呼んでよい)。
			 * @param path    .ghost のフルパス
			 * @param data    書き出すゴースト
			 * @param stageId ヘッダへ焼くステージ ID (ハッシュにして格納する)
			 * @return 書けたら true
			 */
			static bool SaveToFile(const char* path, const GhostData& data, const char* stageId);
		};
	}
}
