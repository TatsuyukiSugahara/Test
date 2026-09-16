#include "aq.h"
#include "DeferredLightingPass.h"


namespace aq
{
	namespace rendering
	{
		DeferredLightingPass::DeferredLightingPass(std::shared_ptr<DeferredRenderer> deferred)
			: deferred_(std::move(deferred))
		{
		}


		void DeferredLightingPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::GBuffer0);
			decl.Reads(PassResourceKeys::GBuffer1);
			decl.Reads(PassResourceKeys::GBuffer2);
			decl.Reads(PassResourceKeys::GBuffer3);
			decl.Writes(PassResourceKeys::Scene);
		}


		void DeferredLightingPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                                 PassResources& res, RenderCommandList& outList)
		{
			(void)view;

			deferred_->BuildLightingCommandList(frame, outList, res.Get(PassResourceKeys::Scene));
		}
	}
}
