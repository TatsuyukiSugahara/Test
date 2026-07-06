#include "aq.h"
#include "Mixer3D.h"
#include <cmath>


namespace aq
{
	namespace sound
	{
		namespace
		{
			constexpr float SPEED_OF_SOUND = 343.0f;   // m/s
			constexpr float MIN_FREQ_RATIO = 0.5f;
			constexpr float MAX_FREQ_RATIO = 2.0f;      // XAudio2 既定の最大周波数比

			float Clamp(float v, float lo, float hi)
			{
				return v < lo ? lo : (v > hi ? hi : v);
			}

			// 距離減衰ゲイン（§4）。rolloff は 1.0 固定。
			float ComputeDistanceGain(AttenuationModel model, float distance, float minDist, float maxDist)
			{
				if (model == AttenuationModel::None) {
					return 1.0f;
				}
				if (maxDist <= minDist) {
					return distance <= minDist ? 1.0f : 0.0f;
				}
				const float d = Clamp(distance, minDist, maxDist);

				switch (model)
				{
				case AttenuationModel::Linear:
					return (maxDist - d) / (maxDist - minDist);
				case AttenuationModel::Inverse:
					return minDist / d;
				case AttenuationModel::Exponential:
					return minDist / d * (minDist / d);   // (minDist/d)^2 近似
				default:
					return 1.0f;
				}
			}

			// listener 空間の右方向ベクトル（正規化）。
			math::Vector3 ComputeRight(const math::Vector3& forward, const math::Vector3& up)
			{
				math::Vector3 right;
				right.Cross(forward, up);   // right = forward × up
				right.TryNormalize();
				return right;
			}
		}


		SpatializationResult Mixer3D::Compute(const SpatializationInput& input)
		{
			SpatializationResult result;

			math::Vector3 toSource = input.sourcePos - input.listenerPos;
			const float   distance = toSource.Length();

			// ── 距離減衰 ──
			const float distanceGain = ComputeDistanceGain(input.model, distance, input.minDistance, input.maxDistance);
			result.distanceGain = distanceGain;

			// ── パンニング（定パワー）──
			float pan = 0.0f;   // -1 = 左, +1 = 右
			if (distance > 1.0e-4f)
			{
				math::Vector3 dir = toSource;
				dir.Normalize();
				const math::Vector3 right = ComputeRight(input.listenerForward, input.listenerUp);
				pan = Clamp(dir.Dot(right), -1.0f, 1.0f);
			}
			const float left  = std::sqrt((1.0f - pan) * 0.5f);
			const float right = std::sqrt((1.0f + pan) * 0.5f);
			result.leftGain  = left  * distanceGain;
			result.rightGain = right * distanceGain;

			// ── ドップラー ──
			if (input.dopplerFactor > 0.0f && distance > 1.0e-4f)
			{
				math::Vector3 dir = toSource;
				dir.Normalize();
				const float vListener = dir.Dot(input.listenerVel) * input.dopplerFactor;   // 正 = ソースへ接近
				const float vSource   = dir.Dot(input.sourceVel)   * input.dopplerFactor;   // 正 = ソースが離れる

				const float denom = SPEED_OF_SOUND + vSource;
				if (denom > 1.0e-3f) {
					const float ratio = (SPEED_OF_SOUND + vListener) / denom;
					result.frequencyRatio = Clamp(ratio, MIN_FREQ_RATIO, MAX_FREQ_RATIO);
				}
			}

			return result;
		}
	}
}
