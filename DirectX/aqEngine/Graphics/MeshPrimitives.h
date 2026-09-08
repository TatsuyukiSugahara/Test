/**
 * 手続き生成のプリミティブメッシュ置き場。
 * モデルデータを持たない形状 (草の房など) を VertexData 列 + インデックス列として組み立てる。
 * GPU リソースにもシングルトンにも触れない純 CPU 計算なので、ワーカースレッドからも呼べる。
 * 生成結果は StaticMesh / InstancedStaticMesh の「自作頂点」初期化へそのまま渡す。
 */
#pragma once
#include <cstdint>
#include <vector>
#include "GraphicsTypes.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * 中心で交差する複数枚のクアッドからなる「草の房」メッシュを生成する。
		 * 背面カリング (CULL_MODE_BACK) が有効なので、巻き順を反転したインデックスも足して両面にする
		 * (頂点は共有するので頂点数は増えない)。
		 * @param outVertices 生成した頂点 (uv.y = 根元 0 / 先端 1。風と色グラデーションが参照する)
		 * @param outIndices  生成したインデックス (表面ぶん + 巻き順反転の裏面ぶん)
		 * @param quadCount   交差させるクアッドの枚数 (0 は 1 に丸める)
		 * @param height      房の高さ [m]
		 * @param width       房の根元の幅 [m]
		 */
		void BuildGrassTuftMesh(std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
		                        const uint32_t quadCount, const float height, const float width);
	}
}
