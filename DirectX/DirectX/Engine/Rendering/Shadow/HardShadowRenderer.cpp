#include "HardShadowRenderer.h"
#include "ShadowPassCommand.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderFrame.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Lighting.h"
#include <cmath>


namespace engine
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

			return true;
		}


		void HardShadowRenderer::BuildShadowCommandList(
			const RenderFrame&       frame,
			RenderCommandList&       outList,
			graphics::IRenderTarget* mainRT,
			float                    mainViewportW,
			float                    mainViewportH)
		{
			outList.Enqueue<ShadowBeginCommand>(*depthMap_, settings_.resolution);

			for (const RenderItem& item : frame.items) {
				if (item.castShadow) {
					outList.Enqueue<ShadowCastCommand>(item, shadowVS_);
				}
			}

			outList.Enqueue<ShadowEndCommand>(*depthMap_, mainRT, mainViewportW, mainViewportH);
		}


		void HardShadowRenderer::FillShadowCBData(
			const graphics::DirectionalLight& light,
			ShadowCBData&                     outData) const
		{
			// ライト方向を正規化 (1 回のみ)
			math::Vector3 normDir = light.direction;
			normDir.Normalize();

			float dotY = std::abs(normDir.y);

			// up ベクトル: ライト方向が Y 軸と平行な場合は X 軸にフォールバック
			math::Vector3 up = (dotY > 0.99f)
				? math::Vector3(1.f, 0.f, 0.f)
				: math::Vector3(0.f, 1.f, 0.f);

			// ライト位置 = シーン中心からライト方向の逆に farPlane/2 だけ離す
			math::Vector3 scaledDir = normDir;
			scaledDir.Scale(settings_.farPlane * 0.5f);
			math::Vector3 eye = settings_.sceneCenter - scaledDir;

			math::Matrix4x4 lightView, lightProj, lightVP;
			lightView.MakeLookAt(eye, settings_.sceneCenter, up);
			lightProj.MakeOrthographic(
				settings_.orthoWidth, settings_.orthoHeight,
				settings_.nearPlane,  settings_.farPlane);
			lightVP.Mull(lightView, lightProj);

			outData                  = ShadowCBData{};
			outData.lightViewProj[0] = lightVP;
			outData.cascadeSplits    = { settings_.farPlane, 0.f, 0.f, 0.f };
			outData.cascadeCount     = 1;
			outData.depthBias        = settings_.depthBias;
			outData.softness         = settings_.softness;
		}
	}
}
