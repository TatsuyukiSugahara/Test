#include "aq.h"
#include "LightManager.h"

namespace aq
{
	namespace graphics
	{
		LightManager* LightManager::sInstance_ = nullptr;


		LightManager::LightManager()
		{
			// デフォルトライト: 白色ディレクショナル + 薄いアンビエント
			data_.directionals[0].direction = { 0.0f, -1.0f, 0.5f };
			data_.directionals[0].color     = { 1.0f,  1.0f, 1.0f };
			data_.directionals[0].intensity = 1.0f;
			data_.directionalLightCount     = 1;
			data_.ambient.color             = { 0.1f,  0.1f, 0.1f };
			data_.ambient.intensity         = 1.0f;
			data_.globalSpecularScale       = 1.0f;
		}


		void LightManager::AddDirectionalLight(const math::Vector3& direction,
		                                        const math::Vector3& color,
		                                        float                intensity)
		{
			if (data_.directionalLightCount >= MaxDirectionalLights) return;
			auto& dl     = data_.directionals[data_.directionalLightCount++];
			dl.direction = direction;
			dl.color     = color;
			dl.intensity = intensity;
		}


		void LightManager::SetDirectionalLight(uint32_t             index,
		                                        const math::Vector3& direction,
		                                        const math::Vector3& color,
		                                        float                intensity)
		{
			if (index >= MaxDirectionalLights) return;
			auto& dl     = data_.directionals[index];
			dl.direction = direction;
			dl.color     = color;
			dl.intensity = intensity;
			if (index >= data_.directionalLightCount)
				data_.directionalLightCount = index + 1;
		}


		void LightManager::SetDirectionalLightCount(uint32_t count)
		{
			data_.directionalLightCount = std::min(count, MaxDirectionalLights);
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
