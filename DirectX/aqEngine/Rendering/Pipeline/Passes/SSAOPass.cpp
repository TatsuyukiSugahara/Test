#include "aq.h"
#include "SSAOPass.h"
#include "Rendering/IBL/IBLEnvironment.h"


namespace aq
{
	namespace rendering
	{
		SSAOPass::SSAOPass(std::unique_ptr<SSAORenderer>     ssao,
		                   std::shared_ptr<DeferredRenderer> deferred,
		                   std::shared_ptr<IBLEnvironment>   ibl)
			: ssao_(std::move(ssao))
			, deferred_(std::move(deferred))
			, ibl_(std::move(ibl))
		{
		}


		SSAOPass::~SSAOPass() = default;


		bool SSAOPass::IsSupported() const
		{
			// AO の計算とぼかしは compute(P2)。非対応環境(FL11_0 未満の Xbox One UWP 等)ではパスごと外す。
			return graphics::IsComputeSupported();
		}


		void SSAOPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::GBuffer0);
			decl.Reads(PassResourceKeys::GBuffer1);
			decl.Reads(PassResourceKeys::GBuffer2);
			decl.Reads(PassResourceKeys::GBuffer3);
			decl.Writes(PassResourceKeys::Scene);
		}


		bool SSAOPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			(void)res;

			// 失敗しても false を返さない(パイプライン全体を止めない)。SSAORenderer::IsActive が false になり何も積まない。
			if (ssao_ && !ssao_->CreateTargets(width, height)) {
				aq::StartupMark("[pipeline] SSAOPass: AO の RT を作れないので SSAO なしで続行");
			}
			return true;
		}


		bool SSAOPass::PrepareResize(const uint32_t width, const uint32_t height)
		{
			return !ssao_ || ssao_->PrepareResize(width, height);
		}


		void SSAOPass::CommitResize(PassResources& res)
		{
			(void)res;
			if (ssao_) { ssao_->CommitResize(); }
		}


		void SSAOPass::AbortResize()
		{
			if (ssao_) { ssao_->AbortResize(); }
		}


		void SSAOPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                     PassResources& res, RenderCommandList& outList)
		{
			if (!ssao_ || !deferred_) { return; }
			if (!ssao_->IsActive()) { return; }

			// AO の計算とぼかし(compute)。G-Buffer と AO の RT だけを読み書きするので RTV は外さない(§0.5-7)。
			// AO の RT にはこのビューの矩形の中だけを書く。testMode に関係なく積む(1 / 2 では合成が AO を読まないだけ)。
			ssao_->BuildComputeCommandList(*deferred_, frame.camera, view, outList);

			// 契約 3: 前のパスの終わり方に依存せず、自分で RT をバインドしてから描く。
			// 合成は深度を使わないが、ライティング・SSR と同じ組み合わせでバインドしておく。
			if (res.Has(PassResourceKeys::Depth)) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			// 同じビューのライティングが PSMainIBL を積んだか(WasIBLUsed)でエントリを選ぶ(§0.2 / §0.5-3)。
			// IsReady() をここで見直さない(ライティングと判断が食い違うフレームを作らない)。
			ssao_->BuildCompositeCommandList(*deferred_, deferred_->WasIBLUsed(), ibl_, outList);
		}
	}
}
