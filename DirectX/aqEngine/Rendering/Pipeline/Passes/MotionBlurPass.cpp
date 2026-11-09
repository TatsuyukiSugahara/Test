#include "aq.h"
#include "MotionBlurPass.h"


namespace aq
{
	namespace rendering
	{
		MotionBlurPass::MotionBlurPass()
			: effect_(std::make_unique<MotionBlurEffect>())
		{
		}


		MotionBlurPass::~MotionBlurPass() = default;


		bool MotionBlurPass::IsSupported() const
		{
			// compute 非対応(FL10 の Xbox One UWP 等)ではモーションブラーが動かない。
			return graphics::IsComputeSupported();
		}


		void MotionBlurPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::WorldPos);
			decl.Writes(PassResourceKeys::PostInput);
		}


		bool MotionBlurPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			if (!effect_ || !effect_->Initialize(width, height)) { return false; }
			Wire(res);
			return true;
		}


		bool MotionBlurPass::PrepareResize(const uint32_t width, const uint32_t height)
		{
			return effect_ && effect_->PrepareResize(width, height);
		}


		void MotionBlurPass::CommitResize(PassResources& res)
		{
			effect_->CommitResize();
			Wire(res);
		}


		void MotionBlurPass::AbortResize()
		{
			if (effect_) { effect_->AbortResize(); }
		}


		void MotionBlurPass::Wire(PassResources& res)
		{
			// PostInput は Build で毎フレーム登録する。保持する掲示板参照は無い。
			(void)res;
		}


		void MotionBlurPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                           PassResources& res, RenderCommandList& outList)
		{
			// モーションブラー等のカメラ依存パス用に今フレームのカメラを渡す
			// (今の Views 経路はカメラを渡さないので単一ビューのときだけ。旧 PostProcessChain と同じ作法)。
			if (view.IsSingleView()) {
				effect_->SetFrameCamera(frame.camera);
			}

			PostProcessContext context;
			context.sceneRT    = res.Get(PassResourceKeys::Scene);
			context.width      = static_cast<uint32_t>(view.fullWidth);
			context.height     = static_cast<uint32_t>(view.fullHeight);
			context.hasCamera  = view.IsSingleView();
			context.worldPosRT = res.Get(PassResourceKeys::WorldPos);   // 未登録なら INVALID なハンドル

			if (effect_->IsEnabled(context)) {
				res.Set(PassResourceKeys::PostInput, effect_->Build(outList, context, res.Get(PassResourceKeys::Scene)));
			} else {
				res.Set(PassResourceKeys::PostInput, res.Get(PassResourceKeys::Scene));
			}
		}
	}
}
