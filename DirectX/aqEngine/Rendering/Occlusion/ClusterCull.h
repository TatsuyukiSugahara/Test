#pragma once
#include <cstdint>

namespace aq
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		struct RenderItem;
		struct CameraData;

		/** クラスタ(トライアングル)カリングの有効/無効 (描画時に適用する実カリング)。 */
		void SetClusterCullEnabled(bool enabled);
		bool IsClusterCullEnabled();

		/**
		 * クラスタカリングを適用してインデックスバッファをバインドし、描画すべきインデックス数を返す。
		 * レンダースレッド (コマンド Execute) から呼ぶこと: 動的IBの Update が現在フレーム領域へ書かれ安全。
		 *
		 * - クラスタ無効/データ無し → 通常の indexBuffer をバインドし item.indexCount を返す。
		 * - 一部クラスタが不可視  → 可視範囲を cullIndexBuffer へ compact しバインド、削減数を返す。
		 * - 全クラスタ不可視      → 0 を返す (DrawIndexed(0) で何も描かない)。
		 */
		uint32_t BindCulledIndices(graphics::RenderContext& ctx,
		                           const RenderItem& item, const CameraData& camera);
	}
}
