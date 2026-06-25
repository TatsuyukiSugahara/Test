#include "aq.h"
#include "HardShadowRenderer.h"
#include "ShadowPassCommand.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderFrame.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Lighting.h"
#include <cmath>
#include <algorithm>
#ifdef AQ_DEBUG_IMGUI
#include "ShadowDebugPanel.h"
#endif


namespace aq
{
	namespace rendering
	{
		bool HardShadowRenderer::Create(const ShadowSettings& settings, const char* shadowVSPath)
		{
			settings_ = settings;

			depthMap_ = graphics::GraphicsDevice::Get().CreateDepthMap(
				settings_.resolution, settings_.resolution);
			if (!depthMap_) return false;

			auto vs = graphics::GraphicsDevice::Get().CreateShader(
				shadowVSPath, "VSMain", graphics::IShader::ShaderType::VS);
			if (!vs) return false;
			shadowVS_ = std::move(vs);

			// b2 用: ライトスライスインデックスを shadow VS に渡す定数バッファ
			ShadowSliceCBData sliceData{};
			lightSliceCB_ = graphics::GraphicsDevice::Get().CreateConstantBuffer(
				&sliceData, sizeof(sliceData));
			if (!lightSliceCB_) return false;

			return true;
		}


		void HardShadowRenderer::BuildShadowCommandList(
			const RenderFrame& frame,
			RenderCommandList& outList,
			RenderTargetHandle prevHandle,
			float              prevViewportW,
			float              prevViewportH)
		{
			const uint32_t lightCount = frame.lighting.directionalLightCount;

			for (uint32_t li = 0; li < lightCount; ++li)
			{
				outList.Enqueue<ShadowBeginCommand>(
					*depthMap_, settings_.resolution, li, *lightSliceCB_);

				for (const RenderItem& item : frame.items) {
					if (item.castShadow)
						outList.Enqueue<ShadowCastCommand>(item, shadowVS_);
				}
				for (const RenderItem& item : frame.forwardItems) {
					if (item.castShadow)
						outList.Enqueue<ShadowCastCommand>(item, shadowVS_);
				}

				outList.Enqueue<ShadowEndCommand>(
					*depthMap_, prevHandle, prevViewportW, prevViewportH);
			}
		}


		void HardShadowRenderer::FillShadowCBData(
			const graphics::LightingData& lighting,
			ShadowCBData&                 outData) const
		{
			outData = ShadowCBData{};
			outData.depthBias    = settings_.depthBias;
			outData.softness     = settings_.softness;

			const uint32_t count = std::min(lighting.directionalLightCount,
			                                static_cast<uint32_t>(MaxShadowCascades));
			outData.cascadeCount = count;

			for (uint32_t i = 0; i < count; ++i)
			{
				const graphics::DirectionalLight& light = lighting.directionals[i];

				math::Vector3 normDir = light.direction;
				if (!normDir.TryNormalize())
					normDir = math::Vector3(0.f, -1.f, 0.f);

				float dotY = std::abs(normDir.y);
				math::Vector3 up = (dotY > 0.99f)
					? math::Vector3(1.f, 0.f, 0.f)
					: math::Vector3(0.f, 1.f, 0.f);

				math::Vector3 scaledDir = normDir;
				scaledDir.Scale(settings_.farPlane * 0.5f);
				math::Vector3 eye = settings_.sceneCenter - scaledDir;

				math::Matrix4x4 lightView, lightProj, lightVP;
				lightView.MakeLookAt(eye, settings_.sceneCenter, up);
				lightProj.MakeOrthographic(
					settings_.orthoWidth, settings_.orthoHeight,
					settings_.nearPlane,  settings_.farPlane);
				lightVP.Mull(lightView, lightProj);

				outData.lightViewProj[i] = lightVP;
			}

			outData.cascadeSplits = { settings_.farPlane, 0.f, 0.f, 0.f };
		}

#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<IDebugRenderable> HardShadowRenderer::CreateDebugPanel()
		{
			return std::make_unique<ShadowDebugPanel>(settings_, this);
		}
#endif
	}
}
