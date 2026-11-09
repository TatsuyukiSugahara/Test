#include "aq.h"
#include "OutlinePass.h"
#include "Rendering/Pipeline/FullscreenComputeCommand.h"


namespace aq
{
	namespace rendering
	{
		bool OutlinePass::IsSupported() const
		{
			// compute で描くので、compute の無い環境では列から外れる。
			return graphics::IsComputeSupported();
		}


		void OutlinePass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			// ポストプロセスの無い列でも組めるように任意にする(無ければ Scene を直接読む)。
			decl.ReadsOptional(PassResourceKeys::Output);
			// GBuffer を持たない列でも組めるように任意にする(無ければ何もしない)。
			decl.ReadsOptional(PassResourceKeys::GBuffer1);
			decl.Writes(PassResourceKeys::Output);
		}


		bool OutlinePass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			// 法線を書くパスが前に無いなら、何もしないパスとして素通しする
			// (Setup 失敗はパイプライン全体の失敗になるため、ここでは false を返さない)。
			// この判定は Setup で 1 回だけ行い、active_ に保持する(リサイズでは見直さない)。
			active_ = res.Has(PassResourceKeys::GBuffer1);
			if (!active_) {
				aq::StartupMark("[pipeline] OutlinePass: GBuffer1 が無いので無効(フォワードのみの列)");
				return true;
			}

			if (!CreateTargets(width, height)) { return false; }
			Wire(res);
			return true;
		}


		RenderTargetHandle OutlinePass::CreateOutputRT(const uint32_t width, const uint32_t height)
		{
			// 出力 RT。直前の色(トーンマップ後)と同じ LDR。
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
			desc.hasDepth    = false;
			return graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(desc);
		}


		bool OutlinePass::CreateTargets(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			shader_ = gd.CreateShader("aqEngine/Assets/Shader/Outline.fx", "main",
			                          graphics::IShader::ShaderType::CS);
			if (!shader_) { return false; }

			outputRT_ = CreateOutputRT(width, height);
			if (!outputRT_.IsValid()) { return false; }

			params_.width  = width;
			params_.height = height;
			constantBuffer_ = gd.CreateConstantBuffer(&params_, sizeof(params_));
			if (!constantBuffer_) { return false; }
			return true;
		}


		bool OutlinePass::PrepareResize(const uint32_t width, const uint32_t height)
		{
			if (!active_) { return true; }

			AbortResize();   // 前回の一時領域が残っていれば捨てる
			pendingOutputRT_ = CreateOutputRT(width, height);
			if (!pendingOutputRT_.IsValid()) { return false; }

			// 一時領域の寸法を CommitResize で params_ へ反映するため、ここで覚えておく。
			pendingWidth_  = width;
			pendingHeight_ = height;
			return true;
		}


		void OutlinePass::CommitResize(PassResources& res)
		{
			if (!active_) { return; }

			graphics::GraphicsDevice::Get().ReleaseOffscreenRenderTarget(outputRT_);
			outputRT_        = pendingOutputRT_;
			pendingOutputRT_ = RenderTargetHandle();

			params_.width  = pendingWidth_;
			params_.height = pendingHeight_;

			Wire(res);
		}


		void OutlinePass::AbortResize()
		{
			graphics::GraphicsDevice::Get().ReleaseOffscreenRenderTarget(pendingOutputRT_);
			pendingOutputRT_ = RenderTargetHandle();
		}


		void OutlinePass::Wire(PassResources& res)
		{
			if (!active_) { return; }

			normalRT_ = res.Get(PassResourceKeys::GBuffer1);

			// 自分より前のパスが書いた Output を入力にし、以降の Output は自分の RT にする。
			inputRT_ = res.Get(PassResourceKeys::Output);
			res.Set(PassResourceKeys::Output, outputRT_);
		}


		void OutlinePass::Build(RenderFrame& frame, const PassViewInfo& view,
		                        PassResources& res, RenderCommandList& outList)
		{
			(void)frame;

			if (!active_ || !shader_ || !outputRT_.IsValid()) { return; }

			// ポストプロセスが無い列では Scene(毎フレーム差し替わる)をそのまま読む。
			const RenderTargetHandle input = inputRT_.IsValid() ? inputRT_ : res.Get(PassResourceKeys::Scene);
			const RenderTargetHandle inputs[2] = { input, normalRT_ };

			outList.Enqueue<FullscreenComputeCommand>(
				shader_.get(), inputs, 2u, outputRT_,
				constantBuffer_.get(), &params_, static_cast<uint32_t>(sizeof(params_)),
				static_cast<uint32_t>(view.fullWidth), static_cast<uint32_t>(view.fullHeight));

			// 後続の UI が描けるように出力を RTV として戻す(契約 2)。
			outList.Enqueue<SetRenderTargetCommand>(outputRT_);
		}
	}
}
