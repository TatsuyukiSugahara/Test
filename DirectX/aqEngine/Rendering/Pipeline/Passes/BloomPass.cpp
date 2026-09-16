#include "aq.h"
#include "BloomPass.h"


namespace aq
{
	namespace rendering
	{
		BloomPass::BloomPass()
			: effect_(std::make_unique<BloomEffect>())
		{
		}


		BloomPass::~BloomPass() = default;


		bool BloomPass::IsSupported() const
		{
			// compute 必須(輝度抽出 / Dual Blur とも CS)。
			return graphics::IsComputeSupported();
		}


		void BloomPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::PostInput);
			decl.Writes(PassResourceKeys::BloomTexture);
		}


		bool BloomPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			(void)res;
			if (!effect_) { return false; }
			return effect_->Initialize(width, height);
		}


		void BloomPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                      PassResources& res, RenderCommandList& outList)
		{
			(void)frame;

			const RenderTargetHandle input = res.Has(PassResourceKeys::PostInput)
				? res.Get(PassResourceKeys::PostInput)
				: res.Get(PassResourceKeys::Scene);

			PostProcessContext context;
			context.sceneRT = input;
			context.width   = static_cast<uint32_t>(view.fullWidth);
			context.height  = static_cast<uint32_t>(view.fullHeight);

			if (effect_->IsEnabled(context)) {
				res.Set(PassResourceKeys::BloomTexture, effect_->Build(outList, context, input));
			}
			// 無効なら BloomTexture を登録しない(TonemapPass が res.Has() で判定する)。
		}
	}
}
