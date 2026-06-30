#pragma once
#include "Math/Vector.h"
#include "SoundTypes.h"


namespace aq
{
	namespace sound
	{
		// 3D 音響パラメータの入力（リスナーとソースの状態）。
		struct SpatializationInput
		{
			math::Vector3 listenerPos;
			math::Vector3 listenerForward;
			math::Vector3 listenerUp;
			math::Vector3 listenerVel;
			math::Vector3 sourcePos;
			math::Vector3 sourceVel;
			AttenuationModel model        = AttenuationModel::Inverse;
			float            minDistance  = 1.0f;
			float            maxDistance  = 1000.0f;
			float            dopplerFactor = 1.0f;
		};


		// 3D 計算結果。ボイスへ渡す出力行列・周波数比など。
		struct SpatializationResult
		{
			float leftGain       = 1.0f;   // 距離減衰込みの L ゲイン（ステレオ出力）
			float rightGain      = 1.0f;   // 距離減衰込みの R ゲイン
			float frequencyRatio = 1.0f;   // ドップラー比（呼び出し側がピッチを乗算）
			float distanceGain   = 1.0f;   // 非 mono フォールバック用の距離ゲイン
		};


		// プラットフォーム非依存の 3D 音響計算（§4）。X3DAudio に依存しない。
		// 距離減衰・定パワーパンニング・ドップラーをコアで計算する。
		class Mixer3D
		{
		public:
			static SpatializationResult Compute(const SpatializationInput& input);
		};
	}
}
