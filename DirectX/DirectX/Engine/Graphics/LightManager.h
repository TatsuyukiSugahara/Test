#pragma once
#include <cstdint>
#include "Lighting.h"
#include "Math/Vector.h"

namespace engine
{
	namespace graphics
	{
		class LightManager
		{
		private:
			LightingData data_;

		private:
			LightManager();
			~LightManager() = default;

		public:
			// ---- Ambient -------------------------------------------------------
			void SetAmbientColor    (const math::Vector3& color)  { data_.ambient.color     = color;     }
			void SetAmbientIntensity(float intensity)              { data_.ambient.intensity = intensity; }

			// ---- Directional ---------------------------------------------------
			void SetDirectionalDirection(const math::Vector3& dir)   { data_.directional.direction = dir;       }
			void SetDirectionalColor    (const math::Vector3& color) { data_.directional.color     = color;     }
			void SetDirectionalIntensity(float intensity)            { data_.directional.intensity = intensity; }

			// ---- Point Lights --------------------------------------------------
			/** ポイントライトを末尾に追加する。MaxPointLights を超えた場合は無視。 */
			void AddPointLight(const math::Vector3& position,
			                   const math::Vector3& color,
			                   float                intensity,
			                   float                range);

			/** 指定インデックスのポイントライトを上書きする。 */
			void SetPointLight(uint32_t             index,
			                   const math::Vector3& position,
			                   const math::Vector3& color,
			                   float                intensity,
			                   float                range);

			void ClearPointLights() { data_.pointLightCount = 0; }

			uint32_t GetPointLightCount() const { return data_.pointLightCount; }

			// ---- 直接アクセス --------------------------------------------------
			AmbientLight&     Ambient()     { return data_.ambient;     }
			DirectionalLight& Directional() { return data_.directional; }
			PointLight&       PointLightAt(uint32_t index) { return data_.pointLights[index]; }

			/** RenderFrame へコピーする直前に Render() から呼ぶ */
			void SetCameraPosition(const math::Vector3& pos) { data_.cameraPosition = pos; }

			const LightingData& GetLightingData() const { return data_; }

		private:
			static LightManager* sInstance_;

		public:
			static void Initialize()
			{
				if (!sInstance_) sInstance_ = new LightManager();
			}
			static LightManager& Get() { return *sInstance_; }
			static void Finalize()
			{
				if (sInstance_) { delete sInstance_; sInstance_ = nullptr; }
			}
		};
	}
}
