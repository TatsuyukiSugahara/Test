#include "aq.h"
#include "SkyPass.h"


namespace aq
{
	namespace rendering
	{
		SkyPass::SkyPass(std::unique_ptr<SkyRenderer> sky)
			: sky_(std::move(sky))
		{
		}


		void SkyPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::Depth);
			decl.Writes(PassResourceKeys::Scene);
		}


		void SkyPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                    PassResources& res, RenderCommandList& outList)
		{
			(void)view;

			// 契約 3: 前のパスの終わり方に依存せず、自分で RT をバインドしてから描く。
			if (res.Has(PassResourceKeys::Depth)) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			if (!sky_) { return; }
			sky_->BuildCommandList(frame, outList);
		}
	}
}
