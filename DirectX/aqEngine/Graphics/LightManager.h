#pragma once
#include <cstdint>
#include "Lighting.h"
#include "Math/Vector.h"

namespace aq
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
			AmbientLight& Ambient() { return data_.ambient; }

			// ---- Directional (後方互換: index 0 を操作) -----------------------
			void SetDirectionalDirection(const math::Vector3& dir)   { data_.directionals[0].direction = dir;       }
			void SetDirectionalColor    (const math::Vector3& color) { data_.directionals[0].color     = color;     }
			void SetDirectionalIntensity(float intensity)            { data_.directionals[0].intensity = intensity; }
			DirectionalLight& Directional() { return data_.directionals[0]; }

			// ---- Directional (複数対応) -----------------------------------------
			/** 末尾にディレクショナルライトを追加。MaxDirectionalLights を超えた場合は無視 */
			void AddDirectionalLight(const math::Vector3& direction,
			                         const math::Vector3& color,
			                         float                intensity);

			/** 指定インデックスのディレクショナルライトを上書き */
			void SetDirectionalLight(uint32_t             index,
			                         const math::Vector3& direction,
			                         const math::Vector3& color,
			                         float                intensity);

			void     SetDirectionalLightCount(uint32_t count);
			uint32_t GetDirectionalLightCount() const { return data_.directionalLightCount; }
			DirectionalLight& DirectionalAt(uint32_t index) { return data_.directionals[index]; }

			// ---- Global Specular -----------------------------------------------
			void  SetGlobalSpecularScale(float scale) { data_.globalSpecularScale = scale; }
			float GetGlobalSpecularScale() const      { return data_.globalSpecularScale;  }

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
			PointLight& PointLightAt(uint32_t index) { return data_.pointLights[index]; }

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
