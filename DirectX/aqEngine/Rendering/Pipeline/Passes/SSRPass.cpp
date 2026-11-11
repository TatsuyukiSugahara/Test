#include "aq.h"
#include "SSRPass.h"
#include "Rendering/IBL/IBLEnvironment.h"


namespace aq
{
	namespace rendering
	{
		SSRPass::SSRPass(std::unique_ptr<SSRRenderer>      ssr,
		                 std::shared_ptr<DeferredRenderer> deferred,
		                 std::shared_ptr<IBLEnvironment>   ibl)
			: ssr_(std::move(ssr))
			, deferred_(std::move(deferred))
			, ibl_(std::move(ibl))
		{
		}


		SSRPass::~SSRPass() = default;


		bool SSRPass::IsSupported() const
		{
			// トレースは compute(P2)。非対応環境(FL11_0 未満の Xbox One UWP 等)ではパスごと外す。
			return graphics::IsComputeSupported();
		}


		void SSRPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::GBuffer0);
			decl.Reads(PassResourceKeys::GBuffer1);
			decl.Reads(PassResourceKeys::GBuffer2);
			decl.Reads(PassResourceKeys::GBuffer3);
			decl.Writes(PassResourceKeys::Scene);
		}


		void SSRPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                    PassResources& res, RenderCommandList& outList)
		{
			(void)frame;
			(void)view;

			if (!ssr_ || !deferred_ || !ibl_) { return; }
			if (!ssr_->IsEnabled()) { return; }

			// 同じビューの DeferredLightingPass が PSMainIBL を積んだときだけ合成する(§0.2 / §0.6-5)。
			// IsReady() をここで見直さない(ライティングと判断が食い違うフレームを作らない)。
			if (!deferred_->WasIBLUsed()) { return; }

			// 契約 3: 前のパスの終わり方に依存せず、自分で RT をバインドしてから描く。
			// 合成は深度を使わないが、ライティング・空と同じ組み合わせでバインドしておく。
			if (res.Has(PassResourceKeys::Depth)) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			ssr_->BuildCommandList(*deferred_, ibl_, outList);
		}
	}
}
