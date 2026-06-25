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

		// HLSL ShadowDepth.fx の ShadowLightCB (b2) と一致させる
		struct ShadowSliceCBData
		{
			uint32_t lightSlice = 0;
			uint32_t _pad[3]    = {};
		};
	}
}
