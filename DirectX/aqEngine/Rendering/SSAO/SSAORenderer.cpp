#include "aq.h"
#include "SSAORenderer.h"
#include "SSAOCompositeCommand.h"
#include "Graphics/GraphicsTypes.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Rendering/IBL/IBLEnvironment.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			static constexpr const char* SSAO_COMPOSITE_SHADER_PATH = "aqEngine/Assets/Shader/SSAOComposite.fx";
		}


		bool SSAORenderer::Create()
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// SSAOComposite.fx の VS/PS (同期生成)
			compositeVS_ = gd.CreateShader(SSAO_COMPOSITE_SHADER_PATH, "VSMain", graphics::IShader::ShaderType::VS);
			compositePS_ = gd.CreateShader(SSAO_COMPOSITE_SHADER_PATH, "PSMain", graphics::IShader::ShaderType::PS);
			if (!compositeVS_ || !compositePS_) {
				aq::StartupMarkf("[ssao] shader load FAILED (%s) -> ssao disabled", SSAO_COMPOSITE_SHADER_PATH);
				compositeVS_.reset();
				compositePS_.reset();
				return false;
			}

			// PSMainIBL だけが作れなくても SSAO は続ける (IBL を使うビューでは合成しない)
			compositePSIBL_ = gd.CreateShader(SSAO_COMPOSITE_SHADER_PATH, "PSMainIBL", graphics::IShader::ShaderType::PS);
			if (!compositePSIBL_) {
				aq::StartupMarkf("[ssao] PSMainIBL load FAILED (%s) -> ssao is skipped on IBL views", SSAO_COMPOSITE_SHADER_PATH);
			}

			aq::StartupMark("[ssao] created (composite VS/PS ok)");
			return true;
		}


		RenderTargetHandle SSAORenderer::CreateAORTInternal(const uint32_t width, const uint32_t height)
		{
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R32_Float;  // 1 チャンネル (遮蔽されない割合)
			desc.hasDepth    = false;
			return graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(desc);
		}


		bool SSAORenderer::CreateTargets(const uint32_t width, const uint32_t height)
		{
			// シェーダが無い (Create 前・失敗) なら RT も作らない
			if (!compositeVS_) { return false; }

			auto& gd = graphics::GraphicsDevice::Get();
			rawAORTHandle_     = CreateAORTInternal(width, height);
			blurredAORTHandle_ = CreateAORTInternal(width, height);
			if (!rawAORTHandle_.IsValid() || !blurredAORTHandle_.IsValid()) {
				aq::StartupMark("[ssao] AO RT create FAILED -> ssao disabled");
				if (rawAORTHandle_.IsValid())     { gd.ReleaseOffscreenRenderTarget(rawAORTHandle_); }
				if (blurredAORTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(blurredAORTHandle_); }
				rawAORTHandle_     = RenderTargetHandle();
				blurredAORTHandle_ = RenderTargetHandle();
				return false;
			}
			return true;
		}


		bool SSAORenderer::PrepareResize(const uint32_t width, const uint32_t height)
		{
			AbortResize();

			// RT を持たない (生成に失敗して無効の) ときは何もしない
			if (!rawAORTHandle_.IsValid() || !blurredAORTHandle_.IsValid()) { return true; }

			pendingRawAORTHandle_     = CreateAORTInternal(width, height);
			pendingBlurredAORTHandle_ = CreateAORTInternal(width, height);
			if (!pendingRawAORTHandle_.IsValid() || !pendingBlurredAORTHandle_.IsValid()) {
				AbortResize();
				EngineAssertMsg(false, "SSAO の AO RT のリサイズ用生成に失敗しました");
				return false;
			}
			return true;
		}


		void SSAORenderer::CommitResize()
		{
			if (!pendingRawAORTHandle_.IsValid() || !pendingBlurredAORTHandle_.IsValid()) { return; }   // 無効 (RT 無し) なら何もしない

			auto& gd = graphics::GraphicsDevice::Get();
			if (rawAORTHandle_.IsValid())     { gd.ReleaseOffscreenRenderTarget(rawAORTHandle_); }
			if (blurredAORTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(blurredAORTHandle_); }
			rawAORTHandle_            = pendingRawAORTHandle_;
			blurredAORTHandle_        = pendingBlurredAORTHandle_;
			pendingRawAORTHandle_     = RenderTargetHandle();
			pendingBlurredAORTHandle_ = RenderTargetHandle();
		}


		void SSAORenderer::AbortResize()
		{
			auto& gd = graphics::GraphicsDevice::Get();
			if (pendingRawAORTHandle_.IsValid())     { gd.ReleaseOffscreenRenderTarget(pendingRawAORTHandle_); }
			if (pendingBlurredAORTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(pendingBlurredAORTHandle_); }
			pendingRawAORTHandle_     = RenderTargetHandle();
			pendingBlurredAORTHandle_ = RenderTargetHandle();
		}


		bool SSAORenderer::IsActive() const
		{
			return enabled_
			    && compositeVS_
			    && compositePS_
			    && rawAORTHandle_.IsValid()
			    && blurredAORTHandle_.IsValid();
		}


		void SSAORenderer::BuildCompositeCommandList(const DeferredRenderer& deferred,
		                                             const bool useIBL,
		                                             const std::shared_ptr<const IBLEnvironment>& ibl,
		                                             RenderCommandList& outList) const
		{
			if (!IsActive()) { return; }

			// P1 は AO を計算しないので、通常 (ぼかした AO を読む) では積まない (設計書 §2 P1。P2 で外す)
			if (testMode_ == TEST_MODE_NONE) { return; }

			// 強度・testMode・デバッグ表示は値で写す (§0.2)
			if (useIBL) {
				if (!ibl || !compositePSIBL_) { return; }
				outList.Enqueue<SSAOCompositeCommand>(
					*compositeVS_, *compositePSIBL_,
					deferred.GetGBuffer0Handle(), deferred.GetGBuffer1Handle(),
					deferred.GetGBuffer2Handle(), deferred.GetGBuffer3Handle(),
					blurredAORTHandle_,
					ibl, ibl->GetIntensity(), testMode_, debugView_);
			} else {
				outList.Enqueue<SSAOCompositeCommand>(
					*compositeVS_, *compositePS_,
					deferred.GetGBuffer0Handle(), deferred.GetGBuffer1Handle(),
					deferred.GetGBuffer2Handle(), deferred.GetGBuffer3Handle(),
					blurredAORTHandle_,
					nullptr, 1.0f, testMode_, debugView_);
			}
		}
	}
}
