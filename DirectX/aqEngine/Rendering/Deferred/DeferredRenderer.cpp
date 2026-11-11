#include "aq.h"
#include "DeferredRenderer.h"
#include "GBufferItemCommand.h"
#include "DeferredLightingCommand.h"
#include "DeferredDecalCommand.h"
#include "Rendering/SetBlendModeCommand.h"
#include "Graphics/GraphicsTypes.h"
#include "Rendering/IBL/IBLEnvironment.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Deferred/Debug/DeferredDebugPanel.h"
#endif


namespace aq
{
	namespace rendering
	{
		bool DeferredRenderer::CreateTargets(uint32_t width, uint32_t height, Targets& out)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// GBuffer0: RGBA8 + depth
			graphics::RenderTargetDesc desc0;
			desc0.width       = width;
			desc0.height      = height;
			desc0.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
			desc0.hasDepth    = true;
			out.g0            = gd.CreateOffscreenRenderTarget(desc0);

			// GBuffer1-3: RGBA16F (depth なし)
			graphics::RenderTargetDesc descF;
			descF.width       = width;
			descF.height      = height;
			descF.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;
			descF.hasDepth    = false;
			out.g1            = gd.CreateOffscreenRenderTarget(descF);
			out.g2            = gd.CreateOffscreenRenderTarget(descF);
			out.g3            = gd.CreateOffscreenRenderTarget(descF);

			if (out.g0.index == ~0u || out.g1.index == ~0u ||
			    out.g2.index == ~0u || out.g3.index == ~0u)
			{
				ReleaseTargets(out);
				return false;
			}
			return true;
		}


		void DeferredRenderer::ReleaseTargets(Targets& targets)
		{
			auto& gd = graphics::GraphicsDevice::Get();
			gd.ReleaseOffscreenRenderTarget(targets.g0);
			gd.ReleaseOffscreenRenderTarget(targets.g1);
			gd.ReleaseOffscreenRenderTarget(targets.g2);
			gd.ReleaseOffscreenRenderTarget(targets.g3);
			targets = Targets();
		}


		bool DeferredRenderer::Create(uint32_t width, uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			Targets targets;
			if (!CreateTargets(width, height, targets)) { return false; }
			gbuffer0Handle_ = targets.g0;
			gbuffer1Handle_ = targets.g1;
			gbuffer2Handle_ = targets.g2;
			gbuffer3Handle_ = targets.g3;

			// PBR ディファードライティングシェーダーをロード（同期）
			auto vs = gd.CreateShader("aqEngine/Assets/Shader/PBRLighting.fx", "VSMain",
			                          graphics::IShader::ShaderType::VS);
			auto ps = gd.CreateShader("aqEngine/Assets/Shader/PBRLighting.fx", "PSMain",
			                          graphics::IShader::ShaderType::PS);
			if (!vs || !ps) return false;

			lightingVS_ = std::move(vs);
			lightingPS_ = std::move(ps);

			// IBL 用の PS (任意機能: 失敗しても PSMain で継続し、IBL を使わない)。
			// PSMain と別エントリにするのは、未バインドの TextureCube を PSMain に持ち込まないため (IBL設計 §0.2)。
			lightingIBLPS_ = gd.CreateShader("aqEngine/Assets/Shader/PBRLighting.fx", "PSMainIBL",
			                                 graphics::IShader::ShaderType::PS);
			if (!lightingIBLPS_) {
				aq::StartupMark("[ibl] PBRLighting.fx PSMainIBL load FAILED -> ibl disabled");
			}

			// 投影デカール用シェーダー・サンプラー (任意機能: 失敗してもライティングは継続)
			decalVS_ = gd.CreateShader("aqEngine/Assets/Shader/Decal.fx", "VSMain",
			                           graphics::IShader::ShaderType::VS);
			decalPS_ = gd.CreateShader("aqEngine/Assets/Shader/Decal.fx", "PSMain",
			                           graphics::IShader::ShaderType::PS);
			graphics::SamplerDesc decalSamp;
			decalSamp.filter   = graphics::FilterMode::MinMagMipLinear;
			decalSamp.addressU = graphics::AddressMode::Clamp;
			decalSamp.addressV = graphics::AddressMode::Clamp;
			decalSamp.addressW = graphics::AddressMode::Clamp;
			decalSampler_ = gd.CreateSamplerState(decalSamp);

			return true;
		}


		bool DeferredRenderer::PrepareResize(uint32_t width, uint32_t height)
		{
			AbortResize();   // 前回の一時領域が残っていれば捨てる
			return CreateTargets(width, height, pending_);
		}


		void DeferredRenderer::CommitResize()
		{
			Targets old;
			old.g0 = gbuffer0Handle_;
			old.g1 = gbuffer1Handle_;
			old.g2 = gbuffer2Handle_;
			old.g3 = gbuffer3Handle_;
			ReleaseTargets(old);

			gbuffer0Handle_ = pending_.g0;
			gbuffer1Handle_ = pending_.g1;
			gbuffer2Handle_ = pending_.g2;
			gbuffer3Handle_ = pending_.g3;
			pending_ = Targets();
		}


		void DeferredRenderer::AbortResize()
		{
			ReleaseTargets(pending_);
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
			RenderCommandList& outList,
			const bool clearTargets) const
		{
			// MRT バインド（GBuffer0-3）
			const RenderTargetHandle handles[4] = {
				gbuffer0Handle_, gbuffer1Handle_,
				gbuffer2Handle_, gbuffer3Handle_
			};
			outList.Enqueue<SetRenderTargetCommand>(4u, handles);

			// カラー × 4 クリア。クリアはビューポートを無視して全面に効くため、
			// 分割画面の 2 ビュー目以降では省略する（前ビューの結果を消さない）。
			if (clearTargets)
			{
				static const float kClearBlack[4] = { 0.f, 0.f, 0.f, 0.f };
				for (uint32_t i = 0; i < 4; ++i)
					outList.Enqueue<ClearRenderTargetCommand>(i, kClearBlack);
				outList.Enqueue<ClearDepthCommand>();
			}

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

			// IBL が使えるときだけ PSMainIBL と環境を渡す。環境は shared_ptr のままコマンドへ持たせ、
			// 実行待ちの間も資源を生かす (IBL設計 §1.1)。強度は値で写す。
			if (iblEnabled_ && ibl_ && lightingIBLPS_ && ibl_->IsReady())
			{
				outList.Enqueue<DeferredLightingCommand>(
					*lightingVS_, *lightingIBLPS_,
					gbuffer0Handle_, gbuffer1Handle_, gbuffer2Handle_, gbuffer3Handle_,
					frame.camera,
					std::shared_ptr<const IBLEnvironment>(ibl_), ibl_->GetIntensity());
				return;
			}

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
