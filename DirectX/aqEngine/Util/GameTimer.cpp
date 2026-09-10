#include "aq.h"
#include "GameTimer.h"
#include <algorithm>
#include <thread>


namespace aq
{
	namespace util
	{
		namespace
		{
			// sleep_for の分解能は OS 依存で粗い (Windows では既定 1〜15.6ms)。
			// 目標時刻の SPIN_MARGIN 手前までを sleep で粗く待ち、残りはスピンで詰める。
			static constexpr std::chrono::steady_clock::duration SPIN_MARGIN =
				std::chrono::milliseconds(1);

			// sleep の 1 回あたりの長さ。長い sleep は overshoot が大きくなるため小刻みに刻む。
			static constexpr std::chrono::milliseconds SLEEP_CHUNK{ 1 };
		}


		GameTimer::GameTimer()
			: lastTime_      {}
			, deltaTime_     (1.0f / 60.0f)
			, totalTime_     (0.0f)
			, fpsLimitSec_   (0.0f)
			, fpsAccum_      (0.0f)
			, fpsFrameCount_ (0)
			, measuredFPS_   (0.0f)
		{
		}


		void GameTimer::Initialize()
		{
			lastTime_ = Clock::now();
		}


		void GameTimer::Tick()
		{
			// FPS 制限: 目標フレーム時間に達するまで待つ
			if (fpsLimitSec_ > 0.0f)
			{
				const TimePoint target = lastTime_ + std::chrono::duration_cast<Clock::duration>(
					std::chrono::duration<double>(static_cast<double>(fpsLimitSec_)));
				while (true)
				{
					const TimePoint now = Clock::now();
					if (now >= target) break;
					// 残り時間が長ければ sleep、SPIN_MARGIN を切ったらスピンで詰める
					if ((target - now) > SPIN_MARGIN) {
						std::this_thread::sleep_for(SLEEP_CHUNK);
					}
				}
			}

			const TimePoint now = Clock::now();

			const double elapsed = std::chrono::duration<double>(now - lastTime_).count();
			lastTime_ = now;

			deltaTime_ = std::min(static_cast<float>(elapsed), MAX_DELTA_TIME);
			totalTime_ += deltaTime_;

			// FPS 測定 (1 秒ごとに更新)
			fpsAccum_ += deltaTime_;
			++fpsFrameCount_;
			if (fpsAccum_ >= 1.0f)
			{
				measuredFPS_  = static_cast<float>(fpsFrameCount_) / fpsAccum_;
				fpsAccum_     = 0.0f;
				fpsFrameCount_ = 0;
			}
		}


		void GameTimer::SetFPSLimit(float fps)
		{
			fpsLimitSec_ = (fps > 0.0f) ? (1.0f / fps) : 0.0f;
		}
	}
}
