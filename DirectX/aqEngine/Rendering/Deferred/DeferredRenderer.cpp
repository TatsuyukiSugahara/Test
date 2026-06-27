#include "aq.h"
#include "DeferredRenderer.h"
#include "GBufferItemCommand.h"
#include "DeferredLightingCommand.h"
#include "DeferredDecalCommand.h"
#include "Rendering/FrameCommands.h"
#include "Rendering/SetBlendModeCommand.h"
#include "Rendering/RenderCommandList.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Deferred/Debug/DeferredDebugPanel.h"
#endif


namespace aq
{
	namespace rendering
	{
		bool DeferredRenderer::Create(uint32_t width, uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// GBuffer0: RGBA8 + depth
			graphics::RenderTargetDesc desc0;
			desc0.width       = width;
			desc0.height      = height;
			desc0.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
			desc0.hasDepth    = true;
			gbuffer0Handle_   = gd.CreateOffscreenRenderTarget(desc0);

			// GBuffer1-3: RGBA16F (depth なし)
			graphics::RenderTargetDesc descF;
			descF.width       = width;
			descF.height      = height;
			descF.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;
			descF.hasDepth    = false;
			gbuffer1Handle_   = gd.CreateOffscreenRenderTarget(descF);
			gbuffer2Handle_   = gd.CreateOffscreenRenderTarget(descF);
			gbuffer3Handle_   = gd.CreateOffscreenRenderTarget(descF);

			if (gbuffer0Handle_.index == ~0u || gbuffer1Handle_.index == ~0u ||
			    gbuffer2Handle_.index == ~0u || gbuffer3Handle_.index == ~0u)
			{
				return false;
			}

			// PBR ディファードライティングシェーダーをロード（同期）
			auto vs = gd.CreateShader("Assets/Shader/PBRLighting.fx", "VSMain",
			                          graphics::IShader::ShaderType::VS);
			auto ps = gd.CreateShader("Assets/Shader/PBRLighting.fx", "PSMain",
			                          graphics::IShader::ShaderType::PS);
			if (!vs || !ps) return false;

			lightingVS_ = std::move(vs);
			lightingPS_ = std::move(ps);

			// 投影デカール用シェーダー・サンプラー (任意機能: 失敗してもライティングは継続)
			decalVS_ = gd.CreateShader("Assets/Shader/Decal.fx", "VSMain",
			                           graphics::IShader::ShaderType::VS);
			decalPS_ = gd.CreateShader("Assets/Shader/Decal.fx", "PSMain",
			                           graphics::IShader::ShaderType::PS);
			graphics::SamplerDesc decalSamp;
			decalSamp.filter   = graphics::FilterMode::MinMagMipLinear;
			decalSamp.addressU = graphics::AddressMode::Clamp;
			decalSamp.addressV = graphics::AddressMode::Clamp;
			decalSamp.addressW = graphics::AddressMode::Clamp;
			decalSampler_ = gd.CreateSamplerState(decalSamp);

			return true;
		}


		void DeferredRenderer::BuildDecalCommandList(
			const RenderFrame& frame,
			RenderCommandList& outList) const
		{
			if (frame.decalItems.empty()) return;
			if (!decalVS_ || !decalPS_ || !decalSampler_) return;

			// GBuffer0 (albedo + 自前 depth) を RT にバインド。深度テストは各コマンドで無効化。
			outList.Enqueue<SetRenderTargetCommand>(gbuffer0Handle_);

			for (const DecalRenderItem& decal : frame.decalItems)
			{
				outList.Enqueue<DeferredDecalCommand>(
					*decalVS_, *decalPS_, *decalSampler_, decal,
					gbuffer1Handle_, gbuffer2Handle_, gbuffer3Handle_);
			}

			// 後続のライティングパスがブレンドしないよう Opaque に戻す。
			outList.Enqueue<SetBlendModeCommand>(graphics::BlendMode::Opaque);
		}


		void DeferredRenderer::BuildGBufferCommandList(
			const RenderFrame& frame,
			RenderCommandList& outList) const
		{
			// MRT バインド（GBuffer0-3）
			const RenderTargetHandle handles[4] = {
				gbuffer0Handle_, gbuffer1Handle_,
				gbuffer2Handle_, gbuffer3Handle_
			};
			outList.Enqueue<SetRenderTargetCommand>(4u, handles);

			// カラー × 4 クリア
			static const float kClearBlack[4] = { 0.f, 0.f, 0.f, 0.f };
			for (uint32_t i = 0; i < 4; ++i)
				outList.Enqueue<ClearRenderTargetCommand>(i, kClearBlack);
			outList.Enqueue<ClearDepthCommand>();

			// G-Buffer ドローコール（gbufferPS を持つアイテムのみ）
			for (const RenderItem& item : frame.items)
			{
				if (item.gbufferPS)
					outList.Enqueue<GBufferItemCommand>(item, frame.camera);
			}
		}


		void DeferredRenderer::BuildLightingCommandList(
			const RenderFrame& frame,
			RenderCommandList& outList,
			RenderTargetHandle sceneRT) const
		{
			// シーン RT を GBuffer0 の depth と組み合わせてバインド
			outList.Enqueue<SetRenderTargetWithDepthCommand>(sceneRT, gbuffer0Handle_);

			// GBuffer ハンドルをそのまま渡す（Execute() 時に解決）
			outList.Enqueue<DeferredLightingCommand>(
				*lightingVS_, *lightingPS_,
				gbuffer0Handle_, gbuffer1Handle_, gbuffer2Handle_, gbuffer3Handle_,
				frame.camera);
		}


#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<IDebugRenderable> DeferredRenderer::CreateDebugPanel(IShadowRenderer* shadow)
		{
			return std::make_unique<DeferredDebugPanel>(*this, shadow);
		}
#endif
	}
}
