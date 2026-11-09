#include "aq.h"
#include "ForwardPass.h"
#include "Rendering/DrawItemCommand.h"
#include "Rendering/InstancedDrawItemCommand.h"
#include "Rendering/SetBlendModeCommand.h"
#include <algorithm>
#include <cmath>


namespace aq
{
	namespace rendering
	{
		void ForwardPass::SortTranslucentBackToFront(const std::vector<RenderItem>& items,
		                                             const math::Matrix4x4& viewMatrix,
		                                             std::vector<uint32_t>& outOrder)
		{
			struct Key { float viewZ; uint32_t index; };
			std::vector<Key> keys;
			keys.reserve(items.size());

			for (uint32_t i = 0; i < static_cast<uint32_t>(items.size()); ++i)
			{
				const RenderItem& item = items[i];
				if (!item.translucent) { continue; }

				const math::Vector3 local = item.hasBounds ? item.localBounds.center : math::Vector3(0.0f, 0.0f, 0.0f);
				const DirectX::XMVECTOR world = DirectX::XMVector3TransformCoord(local, item.worldMatrix);
				const DirectX::XMVECTOR view  = DirectX::XMVector3TransformCoord(world, viewMatrix);
				float z = DirectX::XMVectorGetZ(view);
				if (!std::isfinite(z)) { z = 0.0f; }   // NaN があると比較が弱順序にならず stable_sort が壊れる
				keys.push_back({ z, i });
			}

			std::stable_sort(keys.begin(), keys.end(),
				[](const Key& a, const Key& b) { return a.viewZ > b.viewZ; });

			outOrder.clear();
			outOrder.reserve(keys.size());
			for (const Key& key : keys) { outOrder.push_back(key.index); }
		}


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
				// 半透明は奥→手前の順(収集順のままだと重なったときに合成順が崩れる)。
				std::vector<uint32_t> order;
				SortTranslucentBackToFront(frame.forwardItems, frame.camera.viewMatrix, order);
				for (const uint32_t index : order) {
					outList.Enqueue<DrawItemCommand>(frame.forwardItems[index], frame.camera);
				}
				outList.Enqueue<SetBlendModeCommand>(graphics::BlendMode::Opaque);
			}

			for (const InstancedRenderItem& item : frame.instancedItems) {
				outList.Enqueue<InstancedDrawItemCommand>(item, frame.camera);
			}
		}
	}
}
