#include "aq.h"
#include "DecalPass.h"


namespace aq
{
	namespace rendering
	{
		DecalPass::DecalPass(std::shared_ptr<DeferredRenderer> deferred)
			: deferred_(std::move(deferred))
		{
		}


		void DecalPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::GBuffer0);
			decl.Reads(PassResourceKeys::GBuffer1);
			decl.Reads(PassResourceKeys::GBuffer2);
			decl.Reads(PassResourceKeys::GBuffer3);
			decl.Writes(PassResourceKeys::GBuffer0);
		}


		void DecalPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                      PassResources& res, RenderCommandList& outList)
		{
			(void)view;
			(void)res;

			deferred_->BuildDecalCommandList(frame, outList);
		}
	}
}
