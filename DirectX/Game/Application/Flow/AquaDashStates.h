#pragma once
#include "GameFlow.h"
#include "Terrain/HeightmapChunk.h"
#include "Component/InstancedPointListComponentSystem.h"   // ワーカーで作る草のベイク済み配置


namespace app
{
	namespace aquadash
	{
		/**
		 * タイトル。ステージ一覧 (StageList.json) を読み、決定入力でローディングへ遷移する。
		 * 選択 UI のリスト化と参加プレイヤー確定は P5/P7 で実装する。
		 */
		class TitleState : public IGameState
		{
		public:
			void OnEnter (GameFlow& flow)                 override;
			void OnUpdate(GameFlow& flow, const float dt) override;
		};




		/**
		 * ローディング。ステージ定義のパース + 地形の CPU 前計算 (ThreadPool) → ワールド生成 →
		 * Level 非同期ロードの順で進め、完了でインゲームへ遷移する。
		 */
		class LoadingState : public IGameState
		{
		private:
			enum class Phase { WarmUp, ParseStage, WaitStage, Streaming };

			/**
			 * ワーカータスクの成果物。ステージ定義と、それから作った地形の CPU 側データ + 草のベイク結果。
			 * ゴースト (P23) もファイル読み込みだけなので同じワーカー経路に相乗りさせる。
			 */
			struct StageLoadResult
			{
				std::shared_ptr<stage::StageData>     stage;
				std::shared_ptr<stage::GhostData>     ghost;        // 自己ベスト。無ければ null (初回プレイ)
				aq::terrain::HeightmapChunk::CpuData  terrainCpu;
				aq::ecs::BakedData                    grassBaked;
				aq::ecs::BakedData                    flowerBaked;
			};

			/** 進行状態 */
			Phase phase_        = Phase::WarmUp;
			int   warmupFrames_ = 0;
			float timer_        = 0.0f;
			bool  levelDoneLogged_ = false;   // 起動計測: Level ロード完了を 1 回だけ記録

			/** 非同期ロード */
			std::string                                     stagePath_;
			std::string                                     stageId_;     // ゴーストの照合用 (StageList の id)
			std::string                                     ghostPath_;   // <ユーザーデータ>/<id>.ghost。空なら保存機能が無効
			std::future<StageLoadResult>                    stageFuture_;


		public:
			void OnEnter (GameFlow& flow)                 override;
			void OnUpdate(GameFlow& flow, const float dt) override;
		};




		/**
		 * インゲーム。経過時間の計時とゴール / 落下の判定を行う。
		 * OnEnter でプレイヤーをスポーン位置へリセットするため、
		 * リザルトの「もう一度」はロードなしで即再開できる。
		 */
		class InGameState : public IGameState
		{
		private:
			/** 計時 */
			float elapsed_ = 0.0f;

			/** ゴーストの記録 (P23)。30Hz でサンプリングし、ベスト更新時だけ書き出す */
			std::vector<stage::GhostSample> recording_;
			float                           sampleTimer_ = 0.0f;   // 前回サンプリングからの経過秒

			/** ゴーストの再生 (P23)。ゴーストが無いプレイでは ghost_ が null */
			std::shared_ptr<stage::GhostData> ghost_;
			float                             ghostDeltaSec_ = 0.0f;   // ゴーストとの時間差 [s] (+ で遅れ)
			bool                              ghostFinished_ = false;  // 記録の終端を過ぎた (非表示中)


		public:
			void OnEnter (GameFlow& flow)                 override;
			void OnUpdate(GameFlow& flow, const float dt) override;


			/**
			 * ゴースト関連 (HUD 表示用)
			 */
		public:
			/** ゴーストとの時間差 [s]。正= ゴーストより遅れている / 負= 勝っている */
			inline float GhostDeltaSec() const { return ghostDeltaSec_; }

			/** ゴーストを再生中か (無い走行では false。HUD の差分表示はこれで出し分ける) */
			inline bool HasGhost() const { return ghost_ != nullptr; }
		};




		/**
		 * リザルト。ゲーム System を停止して結果と 3 択メニュー (もう一度 / 次へ / タイトルへ) を駆動する。
		 * 3D 世界は破棄しないため背景は生きたまま (タイトルへ戻るときのみ破棄)。
		 */
		class ResultState : public IGameState
		{
		private:
			int   cursor_     = 0;
			float prevStickY_ = 0.0f;   // 左スティック上下のエッジ検出用


		public:
			void OnEnter (GameFlow& flow)                 override;
			void OnUpdate(GameFlow& flow, const float dt) override;
		};
	}
}
