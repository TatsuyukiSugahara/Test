#pragma once
#include <windows.h>


namespace aq
{
	namespace util
	{
		/**
		 * ゲームタイマー
		 *
		 * QueryPerformanceCounter ベースの高精度タイマー。
		 * Tick() を毎フレーム呼ぶことでデルタタイムと経過時間を更新する。
		 * SetFPSLimit() でフレームレートを制限できる。
		 */
		class GameTimer
		{
		private:
			static constexpr float MAX_DELTA_TIME = 1.0f / 15.0f;

		private:
			LARGE_INTEGER frequency_;
			LARGE_INTEGER lastCount_;

			float deltaTime_;
			float totalTime_;
			float fpsLimitSec_;

			float fpsAccum_;
			int   fpsFrameCount_;
			float measuredFPS_;

		public:
			GameTimer();

			/** 初期化。Initialize() 前に SetFPSLimit() を呼んでも構わない。 */
			void Initialize();

			/** 毎フレーム呼ぶ。FPS 制限がある場合はここでスピンウェイト。 */
			void Tick();

			/** 前フレームとの経過時間 [秒]。上限 MAX_DELTA_TIME でクランプ済み。 */
			float GetDeltaTime() const { return deltaTime_; }

			/** 起動からの経過時間 [秒] */
			float GetTotalTime() const { return totalTime_; }

			/** 現在の測定 FPS */
			float GetFPS() const { return measuredFPS_; }

			/**
			 * FPS 制限を設定する。
			 * @param fps  目標 FPS。0.0f 以下で無制限。
			 */
			void SetFPSLimit(float fps);
		};
	}
}
