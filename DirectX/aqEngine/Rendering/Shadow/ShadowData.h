#pragma once
#include <cstdint>
#include "Math/Vector.h"
#include "Math/Matrix.h"


namespace aq
{
	namespace rendering
	{
		constexpr uint32_t MaxShadowCascades = 4;

		struct ShadowSettings
		{
			uint32_t      resolution  = 2048;
			float         orthoWidth  = 50.0f;
			float         orthoHeight = 50.0f;
			float         nearPlane   = 1.0f;
			float         farPlane    = 200.0f;
			math::Vector3 sceneCenter = {};
			float         depthBias   = 0.005f;
			float         softness    = 1.0f;
		};

		// HLSL ShadowCB (b3) と完全一致させる（16 byte アライメント）
		struct ShadowCBData
		{
			math::Matrix4x4 lightViewProj[MaxShadowCascades]; // 256 bytes
			math::Vector4   cascadeSplits;                    //  16 bytes (x=cascade0 far, yzw=未使用)
			uint32_t        cascadeCount = 1;                 //   4 bytes
			float           depthBias    = 0.005f;            //   4 bytes
			float           softness     = 1.0f;              //   4 bytes
			uint32_t        pad          = 0;                 //   4 bytes
		};

		static_assert(sizeof(ShadowCBData) == 256 + 16 + 16,
		              "ShadowCBData size mismatch with HLSL ShadowCB");

		/**
		 * 影を無効化する b3 シャドウ CB を返す。
		 *
		 * オフスクリーンのように「影なしの素朴なライティング」で描きたいパイプラインが使う。
		 * lightViewProj を「どのワールド座標も ndc.z = -1 へ落とす」行列にすることで、
		 * ShadowSampling.fx の範囲外判定が常に「影なし (1.0)」を返すようにする
		 * (単位行列のままだと原点付近がシャドウマップ範囲内と判定されてしまう)。
		 */
		inline ShadowCBData MakeNeutralShadowCBData()
		{
			const math::Matrix4x4 outside(
				0.0f, 0.0f,  0.0f, 0.0f,
				0.0f, 0.0f,  0.0f, 0.0f,
				0.0f, 0.0f,  0.0f, 0.0f,
				0.0f, 0.0f, -1.0f, 1.0f);

			ShadowCBData shadow;
			for (uint32_t i = 0; i < MaxShadowCascades; ++i) {
				shadow.lightViewProj[i] = outside;
			}
			shadow.cascadeCount = 0;
			return shadow;
		}

		// HLSL ShadowDepth.fx の ShadowLightCB (b2) と一致させる
		struct ShadowSliceCBData
		{
			uint32_t lightSlice = 0;
			uint32_t _pad[3]    = {};
		};
	}
}
