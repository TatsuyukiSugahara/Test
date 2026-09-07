#pragma once
#include "Math/Vector.h"


namespace app
{
	namespace stage
	{
		/**
		 * コーススプライン。制御点列 (Catmull-Rom) + 各点の up ベクトルから
		 * 弧長テーブルを事前構築し、走行距離 (distance) でフレームを評価する。
		 * ループは up の回転で表現する (設計: Game/Design/AquaDash/02_ECS設計.md)。
		 */
		class CourseSpline
		{
		public:
			/** 評価結果。tangent=進行方向 / up=路面法線 / right=up×tangent (左手系) */
			struct Frame
			{
				aq::math::Vector3 position;
				aq::math::Vector3 tangent;
				aq::math::Vector3 up;
				aq::math::Vector3 right;

				/** この基底に沿った回転クォータニオン (forward=tangent, up=up) */
				aq::math::Quaternion ToRotation() const;
			};


		private:
			/** 弧長テーブルの 1 サンプル */
			struct Sample
			{
				aq::math::Vector3 position;
				aq::math::Vector3 tangent;
				aq::math::Vector3 up;
				float             distance;
			};

			std::vector<Sample> samples_;
			float               totalLength_ = 0.0f;


		public:
			/** 制御点列と up 列 (同数) から弧長テーブルを構築する */
			void Build(const std::vector<aq::math::Vector3>& points, const std::vector<aq::math::Vector3>& ups);

			/** 走行距離 [0, TotalLength] のフレームを評価する (範囲外はクランプ) */
			Frame Evaluate(const float distance) const;

			inline float GetTotalLength() const { return totalLength_; }
			inline bool  IsValid()        const { return samples_.size() >= 2; }
		};




		/** コイン配置 (スプライン座標)。判定は P2 で実装する */
		struct CoinPlacement
		{
			float distance = 0.0f;
			float lateral  = 0.0f;
			float height   = 1.0f;
		};


		/** ランクしきい値 (スコア降順に判定) */
		struct RankThreshold
		{
			std::string rank;
			float       score = 0.0f;
		};


		/**
		 * ステージ定義 (*.stage.json)。ロード後は不変データとして扱う。
		 * フォーマットは Game/Design/AquaDash/03_ステージデータ仕様.md が一次資料。
		 */
		struct StageData
		{
			std::string  levelPath;               // 併せてロードする Level (見た目)
			float        width = 12.0f;           // 路面幅 (レーン可動域)
			CourseSpline spline;

			/** スポーン / 判定 */
			float              spawnDistance = 0.0f;
			std::vector<float> spawnLanes;        // プレイヤー毎のレーンオフセット
			float              goalDistance  = 0.0f;
			float              fallHeight    = -30.0f;   // 路面相対でこれを下回ったら落下

			/** 地形の見た目 (省略可。パスが空 / heightScale 0 なら従来どおりの平坦 grass) */
			std::string terrainHeightmapPath;
			std::string terrainSplatmapPath;
			float       terrainHeightScale = 0.0f;
			uint32_t    terrainResolution  = 128;

			/** 評価 */
			std::vector<CoinPlacement> coins;
			float                      parTimeSec = 180.0f;
			std::vector<RankThreshold> ranks;

			/**
			 * ランク判定。score = 0.6×コイン取得率 + 0.4×min(1, parTime/クリアタイム) を
			 * thresholds の上から判定する (設計 03 §3)。該当なしは末尾ランク。
			 */
			std::string CalcRank(const uint32_t coinCount, const float timeSec) const;

			/**
			 * ファイルから読み込む (CPU 処理のみ。ワーカースレッドから呼んでよい)。
			 * 失敗時は nullptr。
			 */
			static std::shared_ptr<StageData> LoadFromFile(const char* path);
		};




		/** StageList.json の 1 エントリ (タイトルの選択肢) */
		struct StageListEntry
		{
			std::string id;
			std::string name;
			std::string stagePath;
			std::string thumbnailPath;
		};


		/**
		 * ステージ一覧のロード。配列順 = 表示順 = 「次のステージへ」の順。
		 */
		class StageRegistry
		{
		public:
			static std::vector<StageListEntry> LoadList(const char* path);
		};
	}
}
