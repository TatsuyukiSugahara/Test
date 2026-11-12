#include "aq.h"
#include "DoFPass.h"


namespace aq
{
	namespace rendering
	{
		DoFPass::DoFPass()
			: effect_(std::make_unique<DoFEffect>())
		{
		}


		DoFPass::~DoFPass() = default;


		bool DoFPass::IsSupported() const
		{
			// compute 非対応(FL10 の Xbox One UWP 等)では DoF が動かない。
			return graphics::IsComputeSupported();
		}


		void DoFPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::PostInput);
			decl.ReadsOptional(PassResourceKeys::WorldPos);
			decl.Writes(PassResourceKeys::PostInput);
			decl.Writes(PassResourceKeys::DoFCoC);
		}


		bool DoFPass::Setup(PassResources& res, const uint32_t width, const uint32_t height)
		{
			// 生成に失敗しても DoFEffect::Initialize は true を返す(無効のまま素通しする)。
			// PostInput は Build で毎フレーム登録するので、ここで掲示板には触らない。
			(void)res;
			return effect_ && effect_->Initialize(width, height);
		}


		bool DoFPass::PrepareResize(const uint32_t width, const uint32_t height)
		{
			return effect_ && effect_->PrepareResize(width, height);
		}


		void DoFPass::CommitResize(PassResources& res)
		{
			// PostInput は Build で毎フレーム登録する。保持する掲示板参照は無い。
			(void)res;
			effect_->CommitResize();
		}


		void DoFPass::AbortResize()
		{
			if (effect_) { effect_->AbortResize(); }
		}


		void DoFPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                    PassResources& res, RenderCommandList& outList)
		{
			// 入力は前段の PostInput(MotionBlur の出力など)。無ければ Scene。
			// PostInput は RenderPipeline がフレームの始めに消すので、ここで見えるのはこのフレームに書かれた値だけ。
			const RenderTargetHandle input = res.Has(PassResourceKeys::PostInput)
				? res.Get(PassResourceKeys::PostInput)
				: res.Get(PassResourceKeys::Scene);

			// Frame scope の viewCount は常に 1 なので、分割画面の判定は frameViewCount で行う
			// (ビューごとにピントが違うので、ビュー 0 のカメラで全画面をぼかさない。設計書 §0.2)。
			const uint32_t frameViewCount = view.frameViewCount;
			const bool     singleView     = (frameViewCount == 1);
			if (singleView) {
				effect_->SetFrameCamera(frame.camera);
			}

			PostProcessContext context;
			context.sceneRT    = input;
			context.width      = static_cast<uint32_t>(view.fullWidth);
			context.height     = static_cast<uint32_t>(view.fullHeight);
			context.hasCamera  = singleView;
			context.worldPosRT = res.Get(PassResourceKeys::WorldPos);   // 未登録なら INVALID なハンドル

			// 毎フレーム必ず PostInput を登録する(実行したら自分の出力、しなかったら読んだ入力)。
			// DoFCoC は動いたフレームだけ登録し、動かないフレームは消す
			// (前フレームの CoC が残ると、DoF を止めても輪郭線がぼけ続けるため。設計書 §0.5)。
			if (effect_->IsEnabled(context)) {
				res.Set(PassResourceKeys::PostInput, effect_->Build(outList, context, input));
				res.Set(PassResourceKeys::DoFCoC, effect_->GetCoCRT());
			} else {
				res.Set(PassResourceKeys::PostInput, input);
				res.Remove(PassResourceKeys::DoFCoC);
			}
		}
	}
}
