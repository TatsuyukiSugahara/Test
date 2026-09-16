#include "aq.h"
#include "ShadowPass.h"


namespace aq
{
	namespace rendering
	{
		ShadowPass::ShadowPass(std::unique_ptr<IShadowRenderer> renderer)
			: renderer_(std::move(renderer))
		{
		}


		void ShadowPass::DeclareResources(PassDeclaration& decl) const
		{
			// シャドウマップは PassResources に載らない(FrameContext 経由)ので宣言なし。
			(void)decl;
		}


		void ShadowPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                       PassResources& res, RenderCommandList& outList)
		{
			if (!renderer_) { return; }

			renderer_->FillShadowCBData(frame.lighting, frame.shadow);
			renderer_->BuildShadowCommandList(frame, outList,
				res.Get(PassResourceKeys::Scene), view.fullWidth, view.fullHeight);
		}
	}
}
