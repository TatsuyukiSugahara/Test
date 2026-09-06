#pragma once
#include "GameFlow.h"


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
		 * ローディング。ステージ定義のパース (ThreadPool) → ワールド生成 →
		 * Level 非同期ロードの順で進め、完了でインゲームへ遷移する。
		 */
		class LoadingState : public IGameState
		{
		private:
			enum class Phase { WarmUp, ParseStage, WaitStage, Streaming };

			/** 進行状態 */
			Phase phase_        = Phase::WarmUp;
			int   warmupFrames_ = 0;
			float timer_        = 0.0f;

			/** 非同期ロード */
			std::string                                     stagePath_;
			std::future<std::shared_ptr<stage::StageData>>  stageFuture_;


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
			float elapsed_ = 0.0f;


		public:
			void OnEnter (GameFlow& flow)                 override;
			void OnUpdate(GameFlow& flow, const float dt) override;
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
