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
			decl.ReadsOptional(PassResourceKeys::WorldPos);
			decl.Writes(PassResourceKeys::Output);
		}


		bool OutlinePass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// worldPos を書くパスが前に無いなら、何もしないパスとして素通しする
			// (Setup 失敗はパイプライン全体の失敗になるため、ここでは false を返さない)。
			if (!res.Has(PassResourceKeys::WorldPos)) {
				aq::StartupMark("[pipeline] OutlinePass: WorldPos が無いので無効(フォワードのみの列)");
				return true;
			}
			worldPosRT_ = res.Get(PassResourceKeys::WorldPos);

			shader_ = gd.CreateShader("aqEngine/Assets/Shader/Outline.fx", "main",
			                          graphics::IShader::ShaderType::CS);
			if (!shader_) { return false; }

			// 出力 RT。直前の色(トーンマップ後)と同じ LDR。
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
			desc.hasDepth    = false;
			outputRT_ = gd.CreateOffscreenRenderTarget(desc);
			if (!outputRT_.IsValid()) { return false; }

			params_.width  = width;
			params_.height = height;
			constantBuffer_ = gd.CreateConstantBuffer(&params_, sizeof(params_));
			if (!constantBuffer_) { return false; }

			// 自分より前のパスが書いた Output を入力にし、以降の Output は自分の RT にする。
			inputRT_ = res.Get(PassResourceKeys::Output);
			res.Set(PassResourceKeys::Output, outputRT_);
			return true;
		}


		void OutlinePass::Build(RenderFrame& frame, const PassViewInfo& view,
		                        PassResources& res, RenderCommandList& outList)
		{
			if (!shader_ || !outputRT_.IsValid()) { return; }

			// ポストプロセスが無い列では Scene(毎フレーム差し替わる)をそのまま読む。
			const RenderTargetHandle input = inputRT_.IsValid() ? inputRT_ : res.Get(PassResourceKeys::Scene);
			const RenderTargetHandle inputs[2] = { input, worldPosRT_ };

			// カメラ位置はフレームごとに変わるので、ここで詰め直す(レンダースレッドから
			// CameraManager を読まないための規約)。
			params_.cameraPos = frame.camera.position;

			outList.Enqueue<FullscreenComputeCommand>(
				shader_.get(), inputs, 2u, outputRT_,
				constantBuffer_.get(), &params_, static_cast<uint32_t>(sizeof(params_)),
				static_cast<uint32_t>(view.fullWidth), static_cast<uint32_t>(view.fullHeight));

			// 後続の UI が描けるように出力を RTV として戻す(契約 2)。
			outList.Enqueue<SetRenderTargetCommand>(outputRT_);
		}
	}
}
