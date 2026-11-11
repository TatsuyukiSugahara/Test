#include "aq.h"
#include "SSRRenderer.h"
#include "SSRTraceCommand.h"
#include "SSRCompositeCommand.h"
#include "Graphics/GraphicsTypes.h"
#include "Rendering/IBL/IBLEnvironment.h"
#ifdef AQ_DEBUG_IMGUI
#include "SSRDebugPanel.h"
#endif


namespace aq
{
	namespace rendering
	{
		namespace
		{
			static constexpr const char* SSR_TRACE_SHADER_PATH     = "aqEngine/Assets/Shader/SSRTrace.fx";
			static constexpr const char* SSR_COMPOSITE_SHADER_PATH = "aqEngine/Assets/Shader/SSRComposite.fx";
		}


		bool SSRRenderer::Create()
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// SSRTrace.fx の CS (同期生成)
			traceCS_ = gd.CreateShader(SSR_TRACE_SHADER_PATH, "main", graphics::IShader::ShaderType::CS);
			if (!traceCS_) {
				aq::StartupMarkf("[ssr] shader load FAILED (%s) -> ssr disabled", SSR_TRACE_SHADER_PATH);
				return false;
			}

			// SSRComposite.fx の VS/PS (同期生成)
			compositeVS_ = gd.CreateShader(SSR_COMPOSITE_SHADER_PATH, "VSMain", graphics::IShader::ShaderType::VS);
			compositePS_ = gd.CreateShader(SSR_COMPOSITE_SHADER_PATH, "PSMain", graphics::IShader::ShaderType::PS);
			if (!compositeVS_ || !compositePS_) {
				aq::StartupMarkf("[ssr] shader load FAILED (%s) -> ssr disabled", SSR_COMPOSITE_SHADER_PATH);
				traceCS_.reset();
				compositeVS_.reset();
				compositePS_.reset();
				return false;
			}

			aq::StartupMark("[ssr] created (trace CS / composite VS/PS ok)");
			return true;
		}


		RenderTargetHandle SSRRenderer::CreateOutputRTInternal(const uint32_t width, const uint32_t height)
		{
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;  // 当たった色 (HDR) + 確信度
			desc.hasDepth    = false;
			return graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(desc);
		}


		bool SSRRenderer::CreateOutputRT(const uint32_t width, const uint32_t height)
		{
			// シェーダが無い (Create 前・失敗) なら RT も作らない
			if (!traceCS_) { return false; }

			outputRTHandle_ = CreateOutputRTInternal(width, height);
			if (!outputRTHandle_.IsValid()) {
				aq::StartupMark("[ssr] output RT create FAILED -> ssr disabled");
				return false;
			}
			return true;
		}


		bool SSRRenderer::PrepareResize(const uint32_t width, const uint32_t height)
		{
			AbortResize();

			// 出力 RT を持たない (生成に失敗して無効の) ときは何もしない
			if (!outputRTHandle_.IsValid()) { return true; }

			pendingOutputRTHandle_ = CreateOutputRTInternal(width, height);
			if (!pendingOutputRTHandle_.IsValid()) {
				EngineAssertMsg(false, "SSR の出力 RT のリサイズ用生成に失敗しました");
				return false;
			}
			return true;
		}


		void SSRRenderer::CommitResize()
		{
			if (!pendingOutputRTHandle_.IsValid()) { return; }   // 無効 (出力 RT 無し) なら何もしない

			auto& gd = graphics::GraphicsDevice::Get();
			if (outputRTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(outputRTHandle_); }
			outputRTHandle_        = pendingOutputRTHandle_;
			pendingOutputRTHandle_ = RenderTargetHandle();
		}


		void SSRRenderer::AbortResize()
		{
			if (pendingOutputRTHandle_.IsValid()) {
				graphics::GraphicsDevice::Get().ReleaseOffscreenRenderTarget(pendingOutputRTHandle_);
			}
			pendingOutputRTHandle_ = RenderTargetHandle();
		}


		bool SSRRenderer::IsActive() const
		{
			return enabled_
			    && traceCS_
			    && compositeVS_
			    && compositePS_
			    && outputRTHandle_.IsValid();
		}


		void SSRRenderer::BuildTraceCommandList(const DeferredRenderer&  deferred,
		                                        const CameraData&        camera,
		                                        const PassViewInfo&      view,
		                                        const RenderTargetHandle sceneRT,
		                                        RenderCommandList&       outList) const
		{
			if (!IsActive()) { return; }

			// パラメータ・行列・矩形は値で写す (デバッグ UI からの変更が実行待ちのコマンドと競合しない。§0.2)
			SSRCBData cbData;
			cbData.viewProj.Mull(camera.viewMatrix, camera.projectionMatrix);
			cbData.view               = camera.viewMatrix;
			cbData.viewRect           = math::Vector4(view.x, view.y, view.w, view.h);
			cbData.screenWidth        = view.fullWidth;
			cbData.screenHeight       = view.fullHeight;
			cbData.maxDistance        = maxDistance_;
			cbData.thicknessScale     = thicknessScale_;
			cbData.maxSteps           = maxSteps_;
			cbData.binarySteps        = binarySteps_;
			cbData.edgeFade           = edgeFade_;
			cbData.nearZ              = camera.nearZ;
			cbData.roughnessFadeStart = roughnessFadeStart_;
			cbData.roughnessFadeEnd   = roughnessFadeEnd_;

			outList.Enqueue<SSRTraceCommand>(
				*traceCS_, sceneRT,
				deferred.GetGBuffer1Handle(), deferred.GetGBuffer2Handle(), deferred.GetGBuffer3Handle(),
				outputRTHandle_, cbData);
		}


		void SSRRenderer::BuildCompositeCommandList(const DeferredRenderer& deferred,
		                                            const std::shared_ptr<const IBLEnvironment>& ibl,
		                                            RenderCommandList& outList) const
		{
			if (!IsActive() || !ibl) { return; }

			// 強度・testMode・デバッグ表示は値で写す (§0.2)
			outList.Enqueue<SSRCompositeCommand>(
				*compositeVS_, *compositePS_,
				deferred.GetGBuffer0Handle(), deferred.GetGBuffer1Handle(),
				deferred.GetGBuffer2Handle(), deferred.GetGBuffer3Handle(),
				outputRTHandle_,
				ibl, ibl->GetIntensity(), testMode_, debugView_);
		}


#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<IDebugRenderable> SSRRenderer::CreateDebugPanel()
		{
			return std::make_unique<SSRDebugPanel>(*this);
		}
#endif
	}
}