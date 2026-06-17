#pragma once
#include <cstdint>
#include "Assets/Shader/MaterialDef.h"
#include "Engine/Math/Vector.h"

namespace aq
{
	namespace graphics
	{
		constexpr uint32_t MaxPointLights = 8;

		enum MaterialFlags : uint32_t
		{
			MatFlag_HasNormal     = 1u << 0,
			MatFlag_HasSpecular   = 1u << 1,
			MatFlag_HasEmissive   = 1u << 2,
			MatFlag_ReceiveShadow = 1u << 3,
			MatFlag_HasSplatMap   = 1u << 4,  // t0=splatmap, t1-t3=layers
			MatFlag_User0         = 1u << 16,
		};

		// GPU レイアウト: HLSL LightingCB の各構造体と完全一致させる
		struct AmbientLight
		{
			math::Vector3 color     = { 0.1f, 0.1f, 0.1f };
			float         intensity = 1.0f;
		};

		struct DirectionalLight
		{
			math::Vector3 direction = { 0.0f, -1.0f, 0.0f };
			float         intensity = 1.0f;
			math::Vector3 color     = { 1.0f, 1.0f, 1.0f };
			float         pad       = 0.0f;
		};

		struct PointLight
		{
			math::Vector3 position  = {};
			float         range     = 10.0f;
			math::Vector3 color     = { 1.0f, 1.0f, 1.0f };
			float         intensity = 1.0f;
		};

		// sizeof が HLSL LightingCB と一致している必要がある
		struct LightingData
		{
			AmbientLight     ambient;
			DirectionalLight directional;
			PointLight       pointLights[MaxPointLights];
			uint32_t         pointLightCount = 0;
			math::Vector3    cameraPosition  = {};
		};

		// sizeof が HLSL MaterialCB と一致している必要がある
		struct MaterialCBData
		{
			float         specularIntensity = 1.0f;
			float         gloss             = 0.5f;
			float         emissiveScale     = 1.0f;
			uint32_t      flags             = 0;
			math::Vector4 params[MATERIAL_PARAMETER_NUM] = {};
		};

		static_assert(offsetof(MaterialCBData, params) == 16,
		              "MaterialCBData layout mismatch: params must be at offset 16");
		static_assert(sizeof(MaterialCBData) == 16 + sizeof(math::Vector4) * MATERIAL_PARAMETER_NUM,
		              "MaterialCBData size mismatch with HLSL MaterialCB");
	}
}
