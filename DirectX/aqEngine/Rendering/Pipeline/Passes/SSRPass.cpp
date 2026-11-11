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


		bool SSRPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			(void)res;

			// 失敗しても false を返さない(パイプライン全体を止めない)。SSRRenderer::IsActive が false になり何も積まない。
			if (ssr_ && !ssr_->CreateOutputRT(width, height)) {
				aq::StartupMark("[pipeline] SSRPass: 出力 RT を作れないので SSR なしで続行");
			}
			return true;
		}


		bool SSRPass::PrepareResize(const uint32_t width, const uint32_t height)
		{
			return !ssr_ || ssr_->PrepareResize(width, height);
		}


		void SSRPass::CommitResize(PassResources& res)
		{
			(void)res;
			if (ssr_) { ssr_->CommitResize(); }
		}


		void SSRPass::AbortResize()
		{
			if (ssr_) { ssr_->AbortResize(); }
		}


		void SSRPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                    PassResources& res, RenderCommandList& outList)
		{
			if (!ssr_ || !deferred_ || !ibl_) { return; }
			if (!ssr_->IsActive()) { return; }

			// 同じビューの DeferredLightingPass が PSMainIBL を積んだときだけ合成する(§0.2 / §0.6-5)。
			// IsReady() をここで見直さない(ライティングと判断が食い違うフレームを作らない)。
			if (!deferred_->WasIBLUsed()) { return; }

			// トレース(compute)。Scene を SRV で読むので RTV を外して回す(§0.6-2)。
			// 出力 RT にはこのビューの矩形の中だけを書く。
			ssr_->BuildTraceCommandList(*deferred_, frame.camera, view, res.Get(PassResourceKeys::Scene), outList);

			// 契約 3: 前のパスの終わり方に依存せず、自分で RT をバインドしてから描く(トレースが RTV を外している)。
			// 合成は深度を使わないが、ライティング・空と同じ組み合わせでバインドしておく。
			if (res.Has(PassResourceKeys::Depth)) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			ssr_->BuildCompositeCommandList(*deferred_, ibl_, outList);
		}
	}
}
