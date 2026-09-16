#include "aq.h"
#include "TonemapPass.h"
#include "BloomPass.h"


namespace aq
{
	namespace rendering
	{
		TonemapPass::TonemapPass()
			: effect_(std::make_unique<TonemapEffect>())
		{
		}


		TonemapPass::~TonemapPass() = default;


		bool TonemapPass::IsSupported() const
		{
			// compute 必須(シーン + ブルームの合成が CS)。
			return graphics::IsComputeSupported();
		}


		void TonemapPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::PostInput);
			decl.ReadsOptional(PassResourceKeys::BloomTexture);
			decl.Writes(PassResourceKeys::Output);
		}


		bool TonemapPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			if (!effect_ || !effect_->Initialize(width, height)) { return false; }
			res.Set(PassResourceKeys::Output, effect_->GetFinalRT());
			return true;
		}


		void TonemapPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                        PassResources& res, RenderCommandList& outList)
		{
			(void)frame;

			const RenderTargetHandle input = res.Has(PassResourceKeys::PostInput)
				? res.Get(PassResourceKeys::PostInput)
				: res.Get(PassResourceKeys::Scene);

			if (res.Has(PassResourceKeys::BloomTexture)) {
				const float bloomIntensity = (bloomSource_ && bloomSource_->GetEffect())
					? bloomSource_->GetEffect()->GetIntensity() : 0.0f;
				effect_->SetBloomInput(res.Get(PassResourceKeys::BloomTexture), bloomIntensity);
			} else {
				// 旧 PostProcessChain::BuildPostProcessCommandList と同じ扱い(合成入力=本流、強度 0)。
				effect_->SetBloomInput(input, 0.0f);
			}

			PostProcessContext context;
			context.sceneRT = input;
			context.width   = static_cast<uint32_t>(view.fullWidth);
			context.height  = static_cast<uint32_t>(view.fullHeight);

			// トーンマップは常時有効(HDR → LDR 変換が無いと表示できない)。
			effect_->Build(outList, context, input);

			// ImGui 等その後のパスが最終 RT に描画できるよう RTV として復元する。
			outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Output));
		}
	}
}
