#pragma once
#include <cstdint>
#include "Graphics/MaterialDef.h"
#include "Math/Vector.h"

namespace aq
{
	namespace graphics
	{
		constexpr uint32_t MaxPointLights       = 8;
		constexpr uint32_t MaxDirectionalLights = 4;

		// forward (Blinn-Phong) シェーダー用フラグ
		enum MaterialFlags : uint32_t
		{
			MatFlag_HasNormal     = 1u << 0,
			MatFlag_HasSpecular   = 1u << 1,
			MatFlag_HasEmissive   = 1u << 2,
			MatFlag_ReceiveShadow = 1u << 3,
			MatFlag_HasSplatMap   = 1u << 4,  // t0=splatmap, t1-t3=layers
			MatFlag_User0         = 1u << 16,
		};

		// PBR シェーダー用フラグ（MaterialFlags と同じビット位置を保持）
		enum PBRMaterialFlags : uint32_t
		{
			PBRFlag_HasNormal            = 1u << 0,
			PBRFlag_HasMetallicRoughness = 1u << 1,  // t2: R=metallic, G=roughness
			PBRFlag_HasEmissive          = 1u << 2,
			PBRFlag_ReceiveShadow        = 1u << 3,
			PBRFlag_HasSplatMap          = 1u << 4,  // Terrain 専用
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
			DirectionalLight directionals[MaxDirectionalLights];
			uint32_t         directionalLightCount = 1;
			float            globalSpecularScale   = 1.0f;
			uint32_t         _pad0                 = 0;
			uint32_t         _pad1                 = 0;
			PointLight       pointLights[MaxPointLights];
			uint32_t         pointLightCount       = 0;
			math::Vector3    cameraPosition        = {};
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

		// sizeof が HLSL PBRMaterialCB と一致している必要がある
		// roughness 移行メモ: 旧 gloss からの変換は roughness = 1.0f - gloss
		// SetShaderType() は SetModelPath() より前に呼ぶこと（ロード後の変更は再初期化されない）
		struct PBRMaterialCBData
		{
			float         metallic      = 0.0f;    // [0,1]: 0=誘電体, 1=金属
			float         roughness     = 0.5f;    // [0,1]: 0=鏡面, 1=粗面
			float         emissiveScale = 1.0f;
			uint32_t      flags         = 0;
			float         specular      = 0.5f;    // 誘電体 F0 スケール (0.5 → F0=0.04)
			float         _pad[3]       = {};
			math::Vector4 _extra[7]     = {};      // Phase 2: subsurface, anisotropic, sheen, clearcoat...
		};

		static_assert(sizeof(PBRMaterialCBData) == sizeof(MaterialCBData),
		              "PBRMaterialCBData must match MaterialCBData size for GPU upload via memcpy");
	}
}
