#pragma once
#include <memory>
#include "IShadowRenderer.h"
#include "Graphics/IDepthMap.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ハードシャドウ (PCF、ディレクショナルライト最大 4 本対応) の実装。
		 * 各ディレクショナルライトが深度マップのスライス 0〜3 に書き込む。
		 */
		class HardShadowRenderer final : public IShadowRenderer
		{
		public:
			HardShadowRenderer() = default;
			~HardShadowRenderer() override = default;

			/**
			 * デプスマップと深度パス用 VS を生成する。
			 * @param settings  解像度・投影範囲・バイアスなどの設定
			 * @param shadowVSPath  ShadowDepth.fx へのパス
			 */
			bool Create(const ShadowSettings& settings, const char* shadowVSPath);

			// IShadowRenderer
			void BuildShadowCommandList(
				const RenderFrame& frame,
				RenderCommandList& outList,
				RenderTargetHandle prevHandle,
				float              prevViewportW,
				float              prevViewportH) override;

			void FillShadowCBData(
				const graphics::LightingData& lighting,
				ShadowCBData&                 outData) const override;

			void SetSceneCenter(const math::Vector3& center) override { settings_.sceneCenter = center; }

			ShadowSettings& GetSettingsRef() override { return settings_; }

			graphics::IDepthMap* GetDepthMap() const override { return depthMap_.get(); }

#ifdef AQ_DEBUG_IMGUI
			std::unique_ptr<IDebugRenderable> CreateDebugPanel() override;
#endif

		private:
			std::unique_ptr<graphics::IDepthMap>       depthMap_;
			std::shared_ptr<graphics::IShader>         shadowVS_;
			std::unique_ptr<graphics::IConstantBuffer> lightSliceCB_;
			ShadowSettings                              settings_;
		};
	}
}
