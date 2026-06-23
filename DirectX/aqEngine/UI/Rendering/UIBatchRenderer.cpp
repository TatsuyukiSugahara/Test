#include "aq.h"
#include "UIBatchRenderer.h"
#include "UIBatchRenderCommand.h"
#include "UIRenderPipeline.h"
#include "UI/UIObject.h"
#include "UI/UIContext.h"
#include "UI/Screen/UIScreen.h"
#include "UI/Screen/UIScreenManager.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UINineSliceComponent.h"
#include "UI/Component/UICircleGaugeComponent.h"
#include "UI/Component/UICanvasComponent.h"
#include "Rendering/RenderCommandList.h"
#include <algorithm>
#include <cmath>

namespace aq
{
	namespace ui
	{
		// =========================================================================
		// コンストラクタ
		// =========================================================================

		UIBatchRenderer::UIBatchRenderer()
		{
			pipeline_ = std::make_unique<UIRenderPipeline>();
		}

		UIBatchRenderer::~UIBatchRenderer() = default;


		// =========================================================================
		// 頂点生成ヘルパー
		// =========================================================================

		// スクリーン座標 (px) → NDC (-1 〜 +1)
		// canvas.resolution を画面サイズとして使用
		static math::Vector2 ToNDC(float px, float py, const math::Vector2& res)
		{
			return {
				 (px / res.x) * 2.f - 1.f,
				-((py / res.y) * 2.f - 1.f)  // Y 反転 (D3D は上が +1)
			};
		}

		static void GenerateQuad(
			const RectF&       rect,
			const RectF&       uvRect,
			const math::Vector4& color,
			const math::Vector2& resolution,
			float              fillAmount,
			FillDirection      fillDir,
			std::vector<UIVertex>& outVerts,
			std::vector<uint16_t>& outIdx)
		{
			// fillAmount で右端をクリップ (Right 方向のみ実装、他方向は同様に拡張)
			float clipW = rect.w * std::max(0.f, std::min(1.f, fillAmount));
			float clipUW = uvRect.w * std::max(0.f, std::min(1.f, fillAmount));
			(void)fillDir; // TODO: Left/Up/Down 対応

			float x0 = rect.x,          y0 = rect.y;
			float x1 = rect.x + clipW,  y1 = rect.y + rect.h;
			float u0 = uvRect.x,         v0 = uvRect.y;
			float u1 = uvRect.x + clipUW, v1 = uvRect.y + uvRect.h;

			uint16_t base = static_cast<uint16_t>(outVerts.size());
			outVerts.push_back({ ToNDC(x0, y0, resolution), {u0, v0}, color });
			outVerts.push_back({ ToNDC(x1, y0, resolution), {u1, v0}, color });
			outVerts.push_back({ ToNDC(x1, y1, resolution), {u1, v1}, color });
			outVerts.push_back({ ToNDC(x0, y1, resolution), {u0, v1}, color });
			// 2 triangles: 0-1-2, 0-2-3
			outIdx.insert(outIdx.end(), {
				static_cast<uint16_t>(base + 0),
				static_cast<uint16_t>(base + 1),
				static_cast<uint16_t>(base + 2),
				static_cast<uint16_t>(base + 0),
				static_cast<uint16_t>(base + 2),
				static_cast<uint16_t>(base + 3)
			});
		}

		static void GenerateNineSlice(
			const RectF&          rect,
			const NineSliceBorder& border,
			const math::Vector4&  color,
			const math::Vector2&  resolution,
			float                 fillAmount,
			std::vector<UIVertex>& outVerts,
			std::vector<uint16_t>& outIdx)
		{
			// fillAmount: rect.w 全体に適用 (右端クリップ)
			float totalW = rect.w * std::max(0.f, std::min(1.f, fillAmount));

			// 縦横の 4 分割点 (スクリーン座標)
			float xs[4] = { rect.x, rect.x + border.left,
			                rect.x + totalW - border.right, rect.x + totalW };
			float ys[4] = { rect.y, rect.y + border.top,
			                rect.y + rect.h - border.bottom, rect.y + rect.h };

			// 右ボーダー消滅ガード
			if (xs[2] < xs[1]) xs[2] = xs[1];
			if (xs[3] < xs[2]) xs[3] = xs[2];

			// UV 4 分割点 (0-1 正規化 → テクスチャサイズ依存はシェーダー側)
			// ここでは border をピクセル比率として扱う (テクスチャサイズ=1 と仮定)
			float us[4] = { 0.f, border.left,  1.f - border.right,  1.f };
			float vs[4] = { 0.f, border.top,   1.f - border.bottom, 1.f };

			// 3×3 = 9 quads
			for (int row = 0; row < 3; ++row)
			{
				for (int col = 0; col < 3; ++col)
				{
					uint16_t base = static_cast<uint16_t>(outVerts.size());
					float x0 = xs[col],   y0 = ys[row];
					float x1 = xs[col+1], y1 = ys[row+1];
					float u0 = us[col],   v0 = vs[row];
					float u1 = us[col+1], v1 = vs[row+1];

					outVerts.push_back({ ToNDC(x0, y0, resolution), {u0, v0}, color });
					outVerts.push_back({ ToNDC(x1, y0, resolution), {u1, v0}, color });
					outVerts.push_back({ ToNDC(x1, y1, resolution), {u1, v1}, color });
					outVerts.push_back({ ToNDC(x0, y1, resolution), {u0, v1}, color });

					outIdx.insert(outIdx.end(), {
						static_cast<uint16_t>(base + 0),
						static_cast<uint16_t>(base + 1),
						static_cast<uint16_t>(base + 2),
						static_cast<uint16_t>(base + 0),
						static_cast<uint16_t>(base + 2),
						static_cast<uint16_t>(base + 3)
					});
				}
			}
		}


		// =========================================================================
		// CollectRenderItems
		// =========================================================================

		void UIBatchRenderer::CollectRenderItems(UIScreenManager& screens)
		{
			Clear();
			for (int i = 0; i < screens.StackSize(); ++i)
			{
				UIScreen* screen = screens.GetScreen(i);
				if (!screen || !screen->GetRoot()) continue;
				uint8_t canvasZ = static_cast<uint8_t>(i);
				CollectFromObject(screen->GetRoot(), canvasZ);
			}
			SortItems();
		}

		void UIBatchRenderer::CollectFromObject(UIObject* obj, uint8_t canvasZ)
		{
			if (!obj || !obj->IsActiveInHierarchy()) return;

			auto* transform = obj->GetComponent<UITransformComponent>();
			if (!transform || !transform->active) return;

			// worldRect を localPosition + sizeDelta + pivot から計算
			// (親変換未対応: localPosition をスクリーン直値として扱う)
			{
				const math::Vector2& pv = transform->pivot.pivot;
				transform->worldRect.x  = transform->localPosition.x - transform->sizeDelta.x * pv.x;
				transform->worldRect.y  = transform->localPosition.y - transform->sizeDelta.y * pv.y;
				transform->worldRect.w  = transform->sizeDelta.x;
				transform->worldRect.h  = transform->sizeDelta.y;
			}

			// 解像度取得 (ルートの UICanvasComponent から)
			math::Vector2 resolution = { 1920.f, 1080.f };
			// TODO: Canvas を辿って解像度を取得 (現在は固定値)

			uint32_t sortKey = MakeUISortKey(canvasZ,
				static_cast<uint16_t>(obj->GetSiblingIndex()), 0);

			// UIImageComponent
			if (auto* img = obj->GetComponent<UIImageComponent>())
			{
				UIRenderItem item;
				item.texture       = img->texture;
				item.shaderType    = UIShaderType::Standard;
				item.sortKey       = sortKey;
				item.isTransparent = (img->color.w < 1.f);

				GenerateQuad(transform->worldRect, img->uvRect,
					img->color, resolution,
					img->fillAmount, img->fillDir,
					item.vertices, item.indices);
				items_.push_back(std::move(item));
			}

			// UINineSliceComponent
			if (auto* ns = obj->GetComponent<UINineSliceComponent>())
			{
				UIRenderItem item;
				item.texture       = ns->texture;
				item.shaderType    = UIShaderType::Standard;
				item.sortKey       = sortKey;
				item.isTransparent = (ns->color.w < 1.f);

				GenerateNineSlice(transform->worldRect, ns->border,
					ns->color, resolution, ns->fillAmount,
					item.vertices, item.indices);
				items_.push_back(std::move(item));
			}

			// UICircleGaugeComponent
			if (auto* cg = obj->GetComponent<UICircleGaugeComponent>())
			{
				UIRenderItem item;
				item.texture       = cg->texture;
				item.shaderType    = UIShaderType::CircleGauge;
				item.sortKey       = sortKey;
				item.isTransparent = (cg->color.w < 1.f);
				item.fillAmount    = cg->fillAmount;
				item.startAngle    = cg->startAngle;
				item.clockwise     = cg->clockwise;
				// 円形ゲージは常に fillAmount=1 でクワッドを生成し、PS でクリップする
				GenerateQuad(transform->worldRect, {0.f, 0.f, 1.f, 1.f},
					cg->color, resolution, 1.f, FillDirection::Right,
					item.vertices, item.indices);
				items_.push_back(std::move(item));
			}

			// 子を再帰処理
			for (UIObject* child : obj->GetChildren())
				CollectFromObject(child, canvasZ);
		}

		void UIBatchRenderer::Clear()
		{
			items_.clear();
		}


		// =========================================================================
		// Sort
		// =========================================================================

		void UIBatchRenderer::SortItems()
		{
			// Canvas 順 → sibling 順 → drawOrder (sortKey の数値順)
			std::stable_sort(items_.begin(), items_.end(),
				[](const UIRenderItem& a, const UIRenderItem& b)
				{
					return a.sortKey < b.sortKey;
				});
		}


		// =========================================================================
		// BuildCommandList
		// =========================================================================

		void UIBatchRenderer::BuildCommandList(rendering::RenderCommandList& cmdList)
		{
			if (items_.empty()) return;
			BuildBatches(cmdList);
		}

		void UIBatchRenderer::BuildBatches(rendering::RenderCommandList& cmdList)
		{
			auto payload = std::make_shared<UIBatchPayload>();
			payload->pipeline = pipeline_.get();

			UIDrawRange* currentRange = nullptr;

			for (const auto& item : items_)
			{
				// CircleGauge は CB パラメータが異なるため常に新しい DrawRange を作る
				bool needNewRange = (currentRange == nullptr)
					|| (currentRange->shaderType != item.shaderType)
					|| (currentRange->texture    != item.texture)
					|| (item.shaderType == UIShaderType::CircleGauge);

				if (needNewRange)
				{
					UIDrawRange range;
					range.texture     = item.texture;
					range.shaderType  = item.shaderType;
					range.indexOffset = static_cast<uint32_t>(payload->indices.size());
					range.indexCount  = 0u;
					range.fillAmount  = item.fillAmount;
					range.startAngle  = item.startAngle;
					range.clockwise   = item.clockwise;
					payload->drawRanges.push_back(range);
					currentRange = &payload->drawRanges.back();
				}

				// 頂点を追加 (インデックスをオフセット補正)
				uint16_t baseVtx = static_cast<uint16_t>(payload->vertices.size());
				payload->vertices.insert(payload->vertices.end(),
					item.vertices.begin(), item.vertices.end());
				for (uint16_t idx : item.indices)
					payload->indices.push_back(static_cast<uint16_t>(baseVtx + idx));

				currentRange->indexCount += static_cast<uint32_t>(item.indices.size());
			}

			// コマンドリストに積む (shared_ptr は ~16 bytes → 64KB アリーナに収まる)
			cmdList.Enqueue<UIBatchRenderCommand>(std::move(payload));
		}

	} // namespace ui
} // namespace aq
