#include "aq.h"
#include "VignettePass.h"
#include "Rendering/Pipeline/FullscreenComputeCommand.h"


namespace sample
{
	bool VignettePass::IsSupported() const
	{
		// compute シェーダで描くので、compute の無い環境では自動的に列から外れる。
		return aq::graphics::IsComputeSupported();
	}


	void VignettePass::DeclareResources(aq::rendering::PassDeclaration& decl) const
	{
		using namespace aq::rendering;
		// トーンマップ後の Output を読むが、ポストプロセスの無い列でも組めるように任意にする
		// (無ければ Scene を直接読む)。書くのは新しい Output。
		decl.Reads(PassResourceKeys::Scene);
		decl.ReadsOptional(PassResourceKeys::Output);
		decl.Writes(PassResourceKeys::Output);
	}


	bool VignettePass::Setup(aq::rendering::PassResources& res, const uint32_t width, const uint32_t height)
	{
		using namespace aq::rendering;
		auto& gd = aq::graphics::GraphicsDevice::Get();

		shader_ = gd.CreateShader("aqEngine/Assets/Shader/Vignette.fx", "main",
		                          aq::graphics::IShader::ShaderType::CS);
		if (!shader_) {
			return false;
		}

		// 出力 RT。トーンマップ後と同じ LDR。
		aq::graphics::RenderTargetDesc desc;
		desc.width       = width;
		desc.height      = height;
		desc.colorFormat = aq::graphics::PixelFormat::R8G8B8A8_Unorm;
		desc.hasDepth    = false;
		outputRT_ = gd.CreateOffscreenRenderTarget(desc);
		if (!outputRT_.IsValid()) {
			return false;
		}

		params_.width  = width;
		params_.height = height;
		constantBuffer_ = gd.CreateConstantBuffer(&params_, sizeof(params_));
		if (!constantBuffer_) {
			return false;
		}

		// 自分より前のパス(トーンマップ)が書いた Output を入力にし、以降の Output は自分の RT にする。
		inputRT_ = res.Get(PassResourceKeys::Output);
		res.Set(PassResourceKeys::Output, outputRT_);
		return true;
	}


	void VignettePass::Build(aq::rendering::RenderFrame& frame, const aq::rendering::PassViewInfo& view,
	                         aq::rendering::PassResources& res, aq::rendering::RenderCommandList& outList)
	{
		using namespace aq::rendering;
		(void)frame;

		// ポストプロセスが無い列では Scene(毎フレーム差し替わる)をそのまま読む。
		const RenderTargetHandle input = inputRT_.IsValid() ? inputRT_ : res.Get(PassResourceKeys::Scene);

		outList.Enqueue<FullscreenComputeCommand>(
			shader_.get(), &input, 1u, outputRT_,
			constantBuffer_.get(), &params_, static_cast<uint32_t>(sizeof(params_)),
			static_cast<uint32_t>(view.fullWidth), static_cast<uint32_t>(view.fullHeight));

		// 後続の UI が描けるように出力を RTV として戻す(契約 2)。
		outList.Enqueue<SetRenderTargetCommand>(outputRT_);
	}
}
