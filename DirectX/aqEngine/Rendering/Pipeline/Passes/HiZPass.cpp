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
			if (!CreateTargets(width, height)) { return false; }
			Wire(res);
			return true;
		}


		bool HiZPass::CreateTargets(const uint32_t width, const uint32_t height)
		{
			if (!hiZ_) { return false; }

			if (!hiZ_->Initialize(width, height))
			{
				aq::StartupMarkf("[pipeline] HiZPass: HiZRenderer::Initialize failed (%ux%u)", width, height);
				return false;
			}
			return true;
		}


		bool HiZPass::PrepareResize(const uint32_t width, const uint32_t height)
		{
			if (!hiZ_) { return false; }
			return hiZ_->PrepareResize(width, height);
		}


		void HiZPass::CommitResize(PassResources& res)
		{
			// HiZRenderer は AppHost が生ポインタで登録しているのでインスタンスは変えず、ミップ連鎖だけを入れ替える。
			hiZ_->CommitResize();
			Wire(res);
		}


		void HiZPass::AbortResize()
		{
			if (hiZ_) { hiZ_->AbortResize(); }
		}


		void HiZPass::Wire(PassResources& res)
		{
			res.Set(PassResourceKeys::HiZ, hiZ_->GetReadbackHandle());
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
