#pragma once
#include <memory>
#include <vector>
#include "UI/UITypes.h"
#include "Math/Vector.h"

namespace aq
{
	namespace graphics { class IShaderResourceView; }

	namespace ui
	{
		// UI 頂点フォーマット
		struct UIVertex
		{
			math::Vector2 position; // NDC (-1 〜 +1)
			math::Vector2 uv;
			math::Vector4 color;    // per-vertex tint (アニメーション対応)
		};

		// 1描画単位 (UIBatchRenderer に Submit するデータ)
		struct UIRenderItem
		{
			std::shared_ptr<graphics::IShaderResourceView> texture;
			UIShaderType            shaderType    = UIShaderType::Standard;
			uint32_t                sortKey       = 0; // [canvasZ:8][siblingIdx:16][drawOrder:8]
			bool                    isTransparent = true;

			// CircleGauge 専用 (shaderType == CircleGauge の時のみ使用)
			float fillAmount = 1.f;
			float startAngle = 0.f;
			float clockwise  = 1.f;

			std::vector<UIVertex>   vertices; // 通常: 4頂点, NineSlice: 16頂点
			std::vector<uint16_t>   indices;  // 通常: 6,  NineSlice: 54
		};

		// sortKey 生成ヘルパー
		inline uint32_t MakeUISortKey(uint8_t canvasZ, uint16_t siblingIdx, uint8_t drawOrder)
		{
			return (static_cast<uint32_t>(canvasZ)    << 24)
				 | (static_cast<uint32_t>(siblingIdx) <<  8)
				 |  static_cast<uint32_t>(drawOrder);
		}

	} // namespace ui
} // namespace aq
