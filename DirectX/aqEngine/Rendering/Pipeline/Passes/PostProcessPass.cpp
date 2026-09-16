#include "aq.h"
#include "PostProcessPass.h"


namespace aq
{
	namespace rendering
	{
		PostProcessPass::PostProcessPass(std::unique_ptr<IPostProcessRenderer> pp)
			: pp_(std::move(pp))
		{
		}


		bool PostProcessPass::IsSupported() const
		{
			// compute 非対応(FL10 の Xbox One UWP 等)ではポストプロセス(Bloom)が動かない。
			return graphics::IsComputeSupported();
		}


		void PostProcessPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::WorldPos);
			decl.Writes(PassResourceKeys::Output);
		}


		bool PostProcessPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			(void)width;
			(void)height;

			if (!pp_) { return false; }

			if (res.Has(PassResourceKeys::WorldPos)) {
				pp_->SetWorldPosRT(res.Get(PassResourceKeys::WorldPos));
			}
			res.Set(PassResourceKeys::Output, pp_->GetFinalRT());
			return true;
		}


		void PostProcessPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                            PassResources& res, RenderCommandList& outList)
		{
			// モーションブラー等のカメラ依存パス用に今フレームのカメラを渡す
			// (今の Views 経路はカメラを渡さないので単一ビューのときだけ)。
			if (view.IsSingleView()) {
				pp_->SetFrameCamera(frame.camera);
			}

			pp_->BuildPostProcessCommandList(outList, res.Get(PassResourceKeys::Scene),
				static_cast<uint32_t>(view.fullWidth), static_cast<uint32_t>(view.fullHeight));
		}
	}
}
