#include "aq.h"
#include "ParticlePass.h"
#include "Rendering/ParticleDrawCommand.h"
#include "Rendering/SetBlendModeCommand.h"


namespace aq
{
	namespace rendering
	{
		void ParticlePass::DeclareResources(PassDeclaration& decl) const
		{
			decl.Reads(PassResourceKeys::Scene);
			decl.ReadsOptional(PassResourceKeys::Depth);
			decl.Writes(PassResourceKeys::Scene);
		}


		void ParticlePass::Build(RenderFrame& frame, const PassViewInfo& view,
		                         PassResources& res, RenderCommandList& outList)
		{
			// エミッタ共有の動的 VB への多重書き込みを避けて先頭ビューにのみ描く(既知の制限)。
			if (!view.IsFirstView()) { return; }

			// 契約 3: 前のパスの終わり方に依存せず、自分で RT をバインドしてから描く。
			if (res.Has(PassResourceKeys::Depth)) {
				outList.Enqueue<SetRenderTargetWithDepthCommand>(
					res.Get(PassResourceKeys::Scene), res.Get(PassResourceKeys::Depth));
			} else {
				outList.Enqueue<SetRenderTargetCommand>(res.Get(PassResourceKeys::Scene));
			}

			for (const ParticleRenderItem& item : frame.particleItems) {
				outList.Enqueue<ParticleDrawCommand>(item, frame.camera);
			}
			if (!frame.particleItems.empty()) {
				outList.Enqueue<SetBlendModeCommand>(graphics::BlendMode::Opaque);
			}
		}
	}
}
