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
#include "UI/Component/UITextComponent.h"
#include "UI/Font/FontAssetCache.h"
#include "UI/Font/TextStyleCache.h"
#include "UI/Font/TextLayout.h"
#include "Rendering/RenderCommandList.h"
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
			std::vector<uint16_t>& outIdx,
			float              rotation = 0.f,                  // degrees, Z 回転
			math::Vector2      pivotPx  = {0.f, 0.f})           // 回転中心 (スクリーン px)
		{
			// fillAmount で右端をクリップ (Right 方向のみ実装、他方向は同様に拡張)
			float clipW  = rect.w * std::max(0.f, std::min(1.f, fillAmount));
			float clipUW = uvRect.w * std::max(0.f, std::min(1.f, fillAmount));
			(void)fillDir; // TODO: Left/Up/Down 対応

			float x0 = rect.x,         y0 = rect.y;
			float x1 = rect.x + clipW, y1 = rect.y + rect.h;
			float u0 = uvRect.x,        v0 = uvRect.y;
			float u1 = uvRect.x + clipUW, v1 = uvRect.y + uvRect.h;

			// 4 corners (screen pixels)
			float cx[4] = { x0, x1, x1, x0 };
			float cy[4] = { y0, y0, y1, y1 };

			// Z 回転をピボット中心に適用
			if (rotation != 0.f)
			{
				const float rad  = rotation * (3.14159265f / 180.f);
				const float cosR = std::cos(rad);
				const float sinR = std::sin(rad);
				const float px   = pivotPx.x, py = pivotPx.y;
				for (int i = 0; i < 4; ++i)
				{
					const float dx = cx[i] - px, dy = cy[i] - py;
					cx[i] = px + dx * cosR - dy * sinR;
					cy[i] = py + dx * sinR + dy * cosR;
				}
			}

			const float uvX[4] = { u0, u1, u1, u0 };
			const float uvY[4] = { v0, v0, v1, v1 };

			uint16_t base = static_cast<uint16_t>(outVerts.size());
			for (int i = 0; i < 4; ++i)
				outVerts.push_back({ ToNDC(cx[i], cy[i], resolution), {uvX[i], uvY[i]}, color });

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
			const math::Vector2&  textureSize,
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

			// UV 4 分割点: border (テクセル) をテクスチャサイズで割って 0-1 正規化
			const float tw = textureSize.x > 0.f ? textureSize.x : 1.f;
			const float th = textureSize.y > 0.f ? textureSize.y : 1.f;
			float us[4] = { 0.f, border.left / tw,  1.f - border.right  / tw, 1.f };
			float vs[4] = { 0.f, border.top  / th,  1.f - border.bottom / th, 1.f };

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
				UIObject* root   = screen ? screen->GetRoot() : nullptr;
				if (!root) continue;

				// ルートの UICanvasComponent から解像度を取得
				math::Vector2 canvasRes = { 1920.f, 1080.f };
				if (auto* canvas = root->GetComponent<UICanvasComponent>())
					canvasRes = canvas->resolution;

				const RectF rootRect = { 0.f, 0.f, canvasRes.x, canvasRes.y };
				uint32_t dfsOrder = 0;
				CollectFromObject(root, static_cast<uint8_t>(i), rootRect, canvasRes, dfsOrder);
			}
			SortItems();
		}

		void UIBatchRenderer::CollectFromObject(UIObject* obj, uint8_t canvasZ,
		                                         const RectF& parentRect, const math::Vector2& canvasRes,
		                                         uint32_t& dfsOrder)
		{
			if (!obj || !obj->IsActiveInHierarchy()) return;

			auto* transform = obj->GetComponent<UITransformComponent>();
			if (!transform || !transform->active) return;

			// ---- Anchor ベースの worldRect 計算 (親 rect 基準) -----------------
			// Unity/UE と同じモデル:
			//   点アンカー (min==max の軸):
			//     anchor_screen = parentRect.origin + anchor * parentRect.size
			//     pivot_screen  = anchor_screen + localPosition
			//     rect_edge     = pivot_screen - size * pivot
			//   ストレッチアンカー (min!=max の軸):
			//     size = anchorArea + sizeDelta   (sizeDelta = 0 で親と同サイズ)
			//     left = anchorLeft + localPosition  (マージン)
			// -----------------------------------------------------------------
			float rx, ry, rw, rh;
			{
				const float anchorL  = parentRect.x + transform->anchor.min.x * parentRect.w;
				const float anchorR  = parentRect.x + transform->anchor.max.x * parentRect.w;
				const float anchorT  = parentRect.y + transform->anchor.min.y * parentRect.h;
				const float anchorB  = parentRect.y + transform->anchor.max.y * parentRect.h;
				const bool  stretchX = (anchorL != anchorR);
				const bool  stretchY = (anchorT != anchorB);
				const math::Vector2& pv = transform->pivot.pivot;

				// X 軸
				if (stretchX)
				{
					rw = (anchorR - anchorL) + transform->sizeDelta.x * transform->localScale.x;
					rx = anchorL + transform->localPosition.x;
				}
				else
				{
					rw = transform->sizeDelta.x * transform->localScale.x;
					rx = anchorL + transform->localPosition.x - rw * pv.x;
				}

				// Y 軸
				if (stretchY)
				{
					rh = (anchorB - anchorT) + transform->sizeDelta.y * transform->localScale.y;
					ry = anchorT + transform->localPosition.y;
				}
				else
				{
					rh = transform->sizeDelta.y * transform->localScale.y;
					ry = anchorT + transform->localPosition.y - rh * pv.y;
				}
			}
			transform->worldRect = { rx, ry, rw, rh };

			// 回転中心: pivot のスクリーン位置
			const math::Vector2 pivotPx = {
				rx + rw * transform->pivot.pivot.x,
				ry + rh * transform->pivot.pivot.y
			};

			uint32_t sortKey = MakeUISortKey(canvasZ,
				static_cast<uint16_t>(dfsOrder++), 0);

			// UIImageComponent
			if (auto* img = obj->GetComponent<UIImageComponent>())
			{
				UIRenderItem item;
				item.texture       = img->texture;
				item.shaderType    = UIShaderType::Standard;
				item.sortKey       = sortKey;
				item.isTransparent = (img->color.w < 1.f);

				// flipH/V: UV を反転
				RectF effectiveUV = img->uvRect;
				if (img->flipH) { effectiveUV.x = img->uvRect.x + img->uvRect.w; effectiveUV.w = -img->uvRect.w; }
				if (img->flipV) { effectiveUV.y = img->uvRect.y + img->uvRect.h; effectiveUV.h = -img->uvRect.h; }

				GenerateQuad(transform->worldRect, effectiveUV,
					img->color, canvasRes,
					img->fillAmount, img->fillDir,
					item.vertices, item.indices,
					transform->rotation, pivotPx);
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

				GenerateNineSlice(transform->worldRect, ns->border, ns->textureSize,
					ns->color, canvasRes, ns->fillAmount,
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
					cg->color, canvasRes, 1.f, FillDirection::Right,
					item.vertices, item.indices,
					transform->rotation, pivotPx);
				items_.push_back(std::move(item));
			}

			// UITextComponent
			if (auto* txt = obj->GetComponent<UITextComponent>())
			{
				// TextStyle 解決
				const TextStyle* style = nullptr;
				if (!txt->textStylePath.empty())
					style = &TextStyleCache::Get().Load(txt->textStylePath);

				// フォントパスは TextStyle のみから取得
				const std::string& fontPath = style ? style->fontPath : std::string{};

				if (!fontPath.empty())
				{
					auto fontAsset = FontAssetCache::Get().Load(fontPath);

					if (fontAsset && fontAsset->IsLoaded())
					{
						// フォントサイズ決定: (コンポーネント override > スタイル > デフォルト) × scale
						const float baseFontSize = (txt->fontSize > 0.f) ? txt->fontSize
						                          : (style ? style->fontSize : 24.f);
						const float fontSize = baseFontSize * txt->scale;

						// 塗り色決定
						math::Vector4 fillTop    = (txt->color.w > 0.f) ? txt->color
						                         : (style ? style->fillColor : math::Vector4{1,1,1,1});
						math::Vector4 fillBottom = fillTop;
						if (style && style->gradient.enabled)
						{
							fillTop    = style->gradient.topColor;
							fillBottom = style->gradient.bottomColor;
						}

						// レイアウト
						TextLayoutParams layoutP;
						layoutP.font        = fontAsset.get();
						layoutP.fontSize    = fontSize;
						layoutP.colorTop    = fillTop;
						layoutP.colorBottom = fillBottom;
						layoutP.boxW        = transform->worldRect.w;
						layoutP.boxH        = transform->worldRect.h;
						layoutP.alignH      = txt->alignH;
						layoutP.alignV      = txt->alignV;
						layoutP.wordWrap    = txt->wordWrap;

						const TextLayoutResult layout = TextLayout::Layout(
							txt->content, layoutP,
							transform->worldRect.x + txt->offset.x,
							transform->worldRect.y + txt->offset.y);

						// smoothing = 0.5 / screenPxRange  (screenPxRange = pxRange * fontSize / baseSize)
						const float smoothing = 0.5f / (fontAsset->GetPxRange() * fontSize / fontAsset->GetBaseSize());

						// fill 用パラメータ (shadow は別 quad で描くため無効化)
						SdfTextParams sdfFill;
						sdfFill.smoothing = smoothing;
						if (style && style->outline.enabled)
						{
							sdfFill.outlineColor = style->outline.color;
							sdfFill.outlineWidth = style->outline.width;
						}

						// shadow 用パラメータと canvas offset
						bool  hasShadow   = style && style->shadow.enabled;
						float shadowDx    = 0.f, shadowDy = 0.f;
						SdfTextParams sdfShadow;
						if (hasShadow)
						{
							// TextStyle offset は Y-Up 系 → canvas Y-Down へ変換
							shadowDx = style->shadow.offset.x;
							shadowDy = -style->shadow.offset.y;
							sdfShadow.shadowColor    = style->shadow.color;
							sdfShadow.shadowOffsetUV = { 0.f, 0.f }; // shadow quad は自身の UV をそのままサンプル
							sdfShadow.shadowSoftness = style->shadow.softness;
							sdfShadow.smoothing      = smoothing;
						}

						// shadow は drawOrder=0, fill は drawOrder=1 (shadow が先に描画される)
						const uint32_t fillSortKey = sortKey | 1u;

						// 各グリフを UIRenderItem に変換
						for (const GlyphQuad& gq : layout.quads)
						{
							const auto toNDC = [&](float px, float py) -> math::Vector2
							{
								return { (px / canvasRes.x) * 2.f - 1.f,
								        -((py / canvasRes.y) * 2.f - 1.f) };
							};

							const float u0 = gq.uvX,        v0 = gq.uvY;
							const float u1 = gq.uvX+gq.uvW, v1 = gq.uvY+gq.uvH;

							// shadow quad: fill quad より先 (sortKey drawOrder=0)
							// 同 UV を canvas offset した位置に描き, fill を vertex alpha=0 で無効化
							if (hasShadow)
							{
								const float sx0 = gq.x + shadowDx, sy0 = gq.y + shadowDy;
								const float sx1 = sx0 + gq.w,      sy1 = sy0 + gq.h;
								const math::Vector4 noFill = { 0.f, 0.f, 0.f, 0.f };

								UIRenderItem sh;
								sh.texture       = fontAsset->GetAtlasSRV();
								sh.shaderType    = UIShaderType::SdfText;
								sh.sortKey       = sortKey;
								sh.isTransparent = true;
								sh.vertices = {
									{ toNDC(sx0,sy0), {u0,v0}, noFill },
									{ toNDC(sx1,sy0), {u1,v0}, noFill },
									{ toNDC(sx1,sy1), {u1,v1}, noFill },
									{ toNDC(sx0,sy1), {u0,v1}, noFill },
								};
								sh.indices  = { 0,1,2, 0,2,3 };
								sh.sdfText  = sdfShadow;
								items_.push_back(std::move(sh));
							}

							// fill quad
							{
								const float x0 = gq.x,      y0 = gq.y;
								const float x1 = gq.x+gq.w, y1 = gq.y+gq.h;

								UIRenderItem item;
								item.texture       = fontAsset->GetAtlasSRV();
								item.shaderType    = UIShaderType::SdfText;
								item.sortKey       = fillSortKey;
								item.isTransparent = true;
								item.vertices = {
									{ toNDC(x0,y0), {u0,v0}, gq.colorTop    },
									{ toNDC(x1,y0), {u1,v0}, gq.colorTop    },
									{ toNDC(x1,y1), {u1,v1}, gq.colorBottom },
									{ toNDC(x0,y1), {u0,v1}, gq.colorBottom },
								};
								item.indices  = { 0,1,2, 0,2,3 };
								item.sdfText  = sdfFill;
								items_.push_back(std::move(item));
							}
						}
					}
				}
			}

			// 子を再帰処理: 自分の worldRect を親矩形として渡す
			for (UIObject* child : obj->GetChildren())
				CollectFromObject(child, canvasZ, transform->worldRect, canvasRes, dfsOrder);
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
			// items_ が空でも常にコマンドを積む。
			// UIBatchRenderCommand::Execute が Blend/Depth ステートを既定値に戻し、
			// 次フレームの GBuffer パスへのステート汚染を防ぐ。
			BuildBatches(cmdList);
		}

		void UIBatchRenderer::BuildBatches(rendering::RenderCommandList& cmdList)
		{
			auto payload = std::make_shared<UIBatchPayload>();
			payload->pipeline = pipeline_.get();

			UIDrawRange* currentRange = nullptr;

			for (const auto& item : items_)
			{
				// CircleGauge / SdfText は CB パラメータが異なるため常に新しい DrawRange を作る
				bool needNewRange = (currentRange == nullptr)
					|| (currentRange->shaderType != item.shaderType)
					|| (currentRange->texture    != item.texture)
					|| (item.shaderType == UIShaderType::CircleGauge)
					|| (item.shaderType == UIShaderType::SdfText);

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
					range.sdfText     = item.sdfText;
					payload->drawRanges.push_back(range);
					currentRange = &payload->drawRanges.back();
				}

				// 頂点を追加 (インデックスをオフセット補正)
				uint32_t baseVtxU = static_cast<uint32_t>(payload->vertices.size());
				if (baseVtxU + static_cast<uint32_t>(item.vertices.size()) > UIRenderPipeline::kMaxVertices)
					continue; // 上限超過: スキップ (TODO: バッチ分割)
				uint16_t baseVtx = static_cast<uint16_t>(baseVtxU);
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
