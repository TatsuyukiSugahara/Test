#include "aq.h"
#include "GBufferPass.h"


namespace aq
{
	namespace rendering
	{
		GBufferPass::GBufferPass(std::shared_ptr<DeferredRenderer> deferred)
			: deferred_(std::move(deferred))
		{
		}


		void GBufferPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.Writes(PassResourceKeys::Depth);
			decl.Writes(PassResourceKeys::GBuffer0);
			decl.Writes(PassResourceKeys::GBuffer1);
			decl.Writes(PassResourceKeys::GBuffer2);
			decl.Writes(PassResourceKeys::GBuffer3);
			decl.Writes(PassResourceKeys::WorldPos);
		}


		bool GBufferPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			if (!deferred_) { return false; }

			// 既に Create 済み(GBuffer0 ハンドルが有効)なら作り直さない。
			if (!deferred_->GetGBuffer0Handle().IsValid())
			{
				if (!deferred_->Create(width, height))
				{
					aq::StartupMarkf("[pipeline] GBufferPass: DeferredRenderer::Create failed (%ux%u)", width, height);
					return false;
				}
			}

			// Depth = GBuffer0(深度を持つ RT)。WorldPos = GBuffer2 の別名。
			res.Set(PassResourceKeys::Depth,    deferred_->GetGBuffer0Handle());
			res.Set(PassResourceKeys::GBuffer0, deferred_->GetGBuffer0Handle());
			res.Set(PassResourceKeys::GBuffer1, deferred_->GetGBuffer1Handle());
			res.Set(PassResourceKeys::GBuffer2, deferred_->GetGBuffer2Handle());
			res.Set(PassResourceKeys::GBuffer3, deferred_->GetGBuffer3Handle());
			res.Set(PassResourceKeys::WorldPos, deferred_->GetGBuffer2Handle());
			return true;
		}


		void GBufferPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                        PassResources& res, RenderCommandList& outList)
		{
			(void)res;

			// クリアはビューポートを無視して全面に効くため、先頭ビューのみ行う(今の Views 経路と同じ)。
			deferred_->BuildGBufferCommandList(frame, outList, view.IsFirstView());
		}
	}
}
