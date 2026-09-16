#include "aq.h"
#include "HiZPass.h"


namespace aq
{
	namespace rendering
	{
		HiZPass::HiZPass(std::shared_ptr<HiZRenderer> hiZ)
			: hiZ_(std::move(hiZ))
		{
		}


		bool HiZPass::IsSupported() const
		{
			return graphics::IsComputeSupported();
		}


		void HiZPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::WorldPos);
			decl.Writes(PassResourceKeys::HiZ);
		}


		bool HiZPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			if (!hiZ_) { return false; }

			if (!hiZ_->Initialize(width, height))
			{
				aq::StartupMarkf("[pipeline] HiZPass: HiZRenderer::Initialize failed (%ux%u)", width, height);
				return false;
			}

			res.Set(PassResourceKeys::HiZ, hiZ_->GetReadbackHandle());
			return true;
		}


		void HiZPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                    PassResources& res, RenderCommandList& outList)
		{
			// Hi-Z は単一カメラ前提(今の Application.cpp のコールバック経路と同じ)。
			if (!view.IsSingleView()) { return; }

			hiZ_->BuildCommandList(frame, outList, res.Get(PassResourceKeys::WorldPos));
		}
	}
}
