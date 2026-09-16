#include "aq.h"
#include "OceanPass.h"
#include "Rendering/OceanDrawCommand.h"


namespace aq
{
	namespace rendering
	{
		bool OceanPass::IsSupported() const
		{
			return graphics::IsComputeSupported();
		}


		void OceanPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::Depth);
			decl.Writes(PassResourceKeys::Scene);
		}


		void OceanPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                      PassResources& res, RenderCommandList& outList)
		{
			(void)view;

			// RT は直前のフォワードパスがバインド済みだが、契約 3 に従い自分でもバインドする。
			if (res.Has(PassResourceKeys::Depth)) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			for (const OceanRenderItem& item : frame.oceanItems) {
				outList.Enqueue<OceanDrawCommand>(item, frame.camera);
			}
		}
	}
}
