#include "aq.h"
#include "GameTimer.h"


namespace aq
{
	namespace util
	{
		GameTimer::GameTimer()
			: frequency_    {}
			, lastCount_    {}
			, deltaTime_    (1.0f / 60.0f)
			, totalTime_    (0.0f)
			, fpsLimitSec_  (0.0f)
			, fpsAccum_     (0.0f)
			, fpsFrameCount_(0)
			, measuredFPS_  (0.0f)
		{
		}


		void GameTimer::Initialize()
		{
			QueryPerformanceFrequency(&frequency_);
			QueryPerformanceCounter(&lastCount_);
		}


		void GameTimer::Tick()
		{
			// FPS 制限: 目標フレーム時間に達するまでスピンウェイト
			if (fpsLimitSec_ > 0.0f)
			{
				LARGE_INTEGER now;
				QueryPerformanceCounter(&now);
				while (true)
				{
					QueryPerformanceCounter(&now);
					const double elapsed = static_cast<double>(now.QuadPart - lastCount_.QuadPart)
					                     / static_cast<double>(frequency_.QuadPart);
					if (elapsed >= fpsLimitSec_) break;
					// 残り時間が長ければ Sleep、短ければスピン
					const double remaining = fpsLimitSec_ - elapsed;
					if (remaining > 0.002) Sleep(1);
				}
			}

			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);

			const double elapsed = static_cast<double>(now.QuadPart - lastCount_.QuadPart)
			                     / static_cast<double>(frequency_.QuadPart);
			lastCount_ = now;

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
