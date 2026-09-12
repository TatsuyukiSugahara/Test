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
		 * 非対称な葉の集まりからなる「草の房」メッシュを生成する。
		 * 葉ごとの方位・根元オフセット・傾き・大きさは葉インデックスからの決定的ハッシュで決めるので、
		 * 同じ引数なら常に同じメッシュになる (再挑戦やステージ再入場で見た目が変わらない)。
		 * 放射状に対称な形だと遠目に「緑の円錐」に見えてしまうため、対称性を崩すことが目的。
		 * 背面カリング (CULL_MODE_BACK) が有効なので、巻き順を反転したインデックスも足して両面にする
		 * (頂点は共有するので頂点数は増えない)。
		 * @param outVertices 生成した頂点 (uv.y = 根元 0 / 先端 1。風と色グラデーションが参照する)
		 * @param outIndices  生成したインデックス (表面ぶん + 巻き順反転の裏面ぶん)
		 * @param bladeCount  房を構成する葉の枚数 (0 は 1 に丸める)
		 * @param height      房の高さ [m] (葉ごとにハッシュで 0.55〜1.00 倍される)
		 * @param width       葉の根元の幅 [m] (葉ごとにハッシュで 0.75〜1.25 倍される)
		 */
		void BuildGrassTuftMesh(std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
		                        const uint32_t bladeCount, const float height, const float width);


		/**
		 * 茎を持たない「花冠だけ」の花メッシュを生成する。
		 * 茎を省くのは、花全体を per-instance 色 1 色で塗り切れる形にして既存の InstancedGrass シェーダを
		 * そのまま流用するため (茎と花弁で色を分けると新規シェーダ・新規エントリが必要になる)。
		 * uv.y は 0.80〜1.00 の範囲に焼いてある。InstancedGrass の PS は uv.y で縦グラデーションを掛けて
		 * 根元を暗くするので、0 に近い uv.y だと花がほとんど暗く沈んでしまうため。
		 * 草の房と同じく、巻き順を反転したインデックスも足して両面にする。
		 * @param outVertices 生成した頂点 (uv.y = 0.80〜1.00)
		 * @param outIndices  生成したインデックス (表面ぶん + 巻き順反転の裏面ぶん)
		 * @param petalCount  花弁の枚数 (0 は 1 に丸める)
		 * @param height      花冠の中心の、インスタンス原点からの高さ [m]
		 * @param radius      花弁の先端までの半径 [m]
		 */
		void BuildFlowerMesh(std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
		                     const uint32_t petalCount, const float height, const float radius);
	}
}
