#pragma once


namespace aq
{
	namespace sound
	{
		// 音量を時間補間する小さなエンベロープ（フェードイン/アウト用）。
		// SoundStream / SoundSource が保持し、毎フレーム Update(dt) で進める。
		struct VolumeEnvelope
		{
			float current    = 1.0f;
			float target     = 1.0f;
			float rate       = 0.0f;    // 秒あたりの変化量（符号付き）
			bool  active     = false;
			bool  stopAtEnd  = false;   // フェード完了時に再生を止めたい（フェードアウト）

			// 即時設定（フェードなし）。
			void SetImmediate(float volume)
			{
				current   = volume;
				target    = volume;
				active    = false;
				stopAtEnd = false;
			}

			// seconds 秒かけて current → to へ補間する。seconds<=0 は即時。
			void FadeTo(float to, float seconds, bool stopWhenDone = false)
			{
				target    = to;
				stopAtEnd = stopWhenDone;
				if (seconds <= 0.0f) {
					current = to;
					active  = false;
				}
				else {
					rate   = (to - current) / seconds;
					active = true;
				}
			}

			// 1 フレーム進める。target に到達したら true（その tick で完了）。
			bool Update(float dt)
			{
				if (!active) {
					return false;
				}
				current += rate * dt;
				const bool reached = (rate >= 0.0f) ? (current >= target) : (current <= target);
				if (reached) {
					current = target;
					active  = false;
					return true;
				}
				return false;
			}
		};
	}
}
