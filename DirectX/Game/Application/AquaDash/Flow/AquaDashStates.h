#pragma once
#include "GameFlow.h"


namespace app
{
	namespace aquadash
	{
		/**
		 * タイトル (P0 仮)。決定入力でインゲームへ遷移する。
		 * ステージ選択 (StageRegistry) と参加プレイヤー確定は P1 以降で実装する。
		 */
		class TitleState : public IGameState
		{
		public:
			void OnUpdate(GameFlow& flow, const float dt) override;
		};




		/**
		 * インゲーム (P0 仮)。経過時間の計時のみ行う。
		 * P0 は仮入力 (決定=ゴール / 下=落下) でリザルトへ遷移する。実判定は P3。
		 */
		class InGameState : public IGameState
		{
		private:
			float elapsed_ = 0.0f;


		public:
			void OnEnter (GameFlow& flow)                  override;
			void OnUpdate(GameFlow& flow, const float dt) override;
		};




		/**
		 * リザルト。結果表示と 3 択メニュー (もう一度 / 次へ / タイトルへ) を駆動する。
		 */
		class ResultState : public IGameState
		{
		private:
			int   cursor_     = 0;
			float prevStickY_ = 0.0f;   // 左スティック上下のエッジ検出用


		public:
			void OnEnter (GameFlow& flow)                  override;
			void OnUpdate(GameFlow& flow, const float dt) override;
		};
	}
}
