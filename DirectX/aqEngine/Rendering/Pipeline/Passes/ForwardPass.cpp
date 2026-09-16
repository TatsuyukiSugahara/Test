#include "aq.h"
#include "ForwardPass.h"
#include "Rendering/DrawItemCommand.h"
#include "Rendering/InstancedDrawItemCommand.h"
#include "Rendering/SetBlendModeCommand.h"


namespace aq
{
	namespace rendering
	{
		void ForwardPass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::Depth);
			decl.Writes(PassResourceKeys::Scene);
		}


		void ForwardPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                        PassResources& res, RenderCommandList& outList)
		{
			(void)view;

			// 契約 3: 前のパスの終わり方に依存せず、自分で RT をバインドしてから描く。
			const bool hasDepth = res.Has(PassResourceKeys::Depth);
			if (hasDepth) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			// GBufferPass の無いフォワードのみ構成では frame.items(deferred 用アイテム)も
			// ここでまとめて描く(Renderer::BuildCommandList の deferredRenderer_ == nullptr 分岐と同じ)。
			if (!hasDepth) {
				for (const RenderItem& item : frame.items) {
					outList.Enqueue<DrawItemCommand>(item, frame.camera);
				}
			}

			// forward アイテムを「不透明 → 半透明」の順に積む。半透明の間だけ AlphaBlend にし、
			// 描き終えたら後続パス(海・パーティクル・ポスト・UI)のために Opaque へ戻す。
			// 半透明が 1 つも無ければ BlendMode は一切積まない
			// (= 従来の不透明だけのフレームはコマンド列も従来どおり)。
			bool hasTranslucent = false;
			for (const RenderItem& item : frame.forwardItems) {
				if (item.translucent) {
					hasTranslucent = true;
					continue;
				}
				outList.Enqueue<DrawItemCommand>(item, frame.camera);
			}
			if (hasTranslucent) {
				outList.Enqueue<SetBlendModeCommand>(graphics::BlendMode::AlphaBlend);
				for (const RenderItem& item : frame.forwardItems) {
					if (item.translucent) {
						outList.Enqueue<DrawItemCommand>(item, frame.camera);
					}
				}
				outList.Enqueue<SetBlendModeCommand>(graphics::BlendMode::Opaque);
			}

			for (const InstancedRenderItem& item : frame.instancedItems) {
				outList.Enqueue<InstancedDrawItemCommand>(item, frame.camera);
			}
		}
	}
}
