#include "EnginePreCompile.h"
#include "LightManager.h"

namespace engine
{
	namespace graphics
	{
		LightManager* LightManager::sInstance_ = nullptr;


		LightManager::LightManager()
		{
			// デフォルトライト: 白色ディレクショナル + 薄いアンビエント
			data_.directional.direction = { 0.0f, -1.0f, 0.5f };
			data_.directional.color     = { 1.0f,  1.0f, 1.0f };
			data_.directional.intensity = 1.0f;
			data_.ambient.color         = { 0.1f,  0.1f, 0.1f };
			data_.ambient.intensity     = 1.0f;
		}


		void LightManager::AddPointLight(const math::Vector3& position,
		                                 const math::Vector3& color,
		                                 float                intensity,
		                                 float                range)
		{
			if (data_.pointLightCount >= MaxPointLights) return;
			auto& pl       = data_.pointLights[data_.pointLightCount++];
			pl.position  = position;
			pl.color     = color;
			pl.intensity = intensity;
			pl.range     = range;
		}


		void LightManager::SetPointLight(uint32_t             index,
		                                 const math::Vector3& position,
		                                 const math::Vector3& color,
		                                 float                intensity,
		                                 float                range)
		{
			if (index >= MaxPointLights) return;
			auto& pl       = data_.pointLights[index];
			pl.position  = position;
			pl.color     = color;
			pl.intensity = intensity;
			pl.range     = range;
			if (index >= data_.pointLightCount)
				data_.pointLightCount = index + 1;
		}
	}
}
