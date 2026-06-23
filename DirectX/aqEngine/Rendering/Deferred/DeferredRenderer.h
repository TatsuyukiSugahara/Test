#pragma once
#include <memory>
#include "IDeferredRenderer.h"
#include "Graphics/IShader.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
namespace aq { namespace rendering { class IShadowRenderer; } }
#endif

namespace aq
{
	namespace rendering
	{
		/**
		 * ディファードレンダラー実装。
		 *
		 * 所有するリソース:
		 *   - GBuffer0: RGBA8  (albedo RGB + specMask)      ← depth バッファあり
		 *   - GBuffer1: RGBA16F (normal XY + gloss)
		 *   - GBuffer2: RGBA16F (worldPos XYZ + specularIntensity)
		 *   - GBuffer3: RGBA16F (emissive RGB + pixelTag)
		 *   - DeferredLighting VS/PS シェーダー
		 */
		class DeferredRenderer final : public IDeferredRenderer
		{
		public:
			bool Create(uint32_t width, uint32_t height) override;

			void BuildGBufferCommandList(const RenderFrame& frame,
			                             RenderCommandList& outList) const override;

			void BuildLightingCommandList(const RenderFrame& frame,
			                              RenderCommandList& outList,
			                              RenderTargetHandle sceneRT) const override;

			RenderTargetHandle GetGBuffer0Handle() const override { return gbuffer0Handle_; }
			RenderTargetHandle GetGBuffer1Handle() const { return gbuffer1Handle_; }
			RenderTargetHandle GetGBuffer2Handle() const { return gbuffer2Handle_; }
			RenderTargetHandle GetGBuffer3Handle() const { return gbuffer3Handle_; }

#ifdef AQ_DEBUG_IMGUI
			std::unique_ptr<IDebugRenderable> CreateDebugPanel(IShadowRenderer* shadow = nullptr);
#endif

		private:
			RenderTargetHandle gbuffer0Handle_;
			RenderTargetHandle gbuffer1Handle_;
			RenderTargetHandle gbuffer2Handle_;
			RenderTargetHandle gbuffer3Handle_;

			std::shared_ptr<graphics::IShader> lightingVS_;
			std::shared_ptr<graphics::IShader> lightingPS_;
		};
	}
}
