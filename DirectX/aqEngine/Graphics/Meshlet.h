/**
 * メッシュレット (クラスタ) — トライアングルカリングの単位。
 *
 * メッシュのインデックス列を ~128 三角形ごとのクラスタへ分割し、各クラスタに
 * ローカル AABB と 法線錐 (cone) を持たせる。これにより:
 *   - フラスタムカリング (クラスタ境界 × 視錐台)
 *   - バックフェースカリング (法線錐 × 視線)
 * をクラスタ単位で行える。
 *
 * 現状は CPU 統計用。将来 GPU 駆動 (compute + 間接描画) でも同じデータを使う。
 */
#pragma once
#include <cstdint>
#include <vector>
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Bounds.h"

namespace aq
{
	namespace graphics
	{
		/** 1 クラスタ (メッシュレット) の境界情報。 */
		struct MeshCluster
		{
			math::Vector3 center;       // ローカル AABB 中心
			math::Vector3 extent;       // ローカル AABB 半サイズ
			math::Vector3 coneAxis;     // 平均法線 (ローカル, 正規化)。外向き。
			float         coneCutoff = 2.0f;  // バックフェース判定閾値 (2.0=判定不能で常に可視)
			uint32_t      triOffset = 0;  // クラスタ先頭の三角形インデックス (×3 で index offset)
			uint32_t      triCount  = 0;  // クラスタ内三角形数
		};


		/**
		 * 頂点位置とインデックスから、trisPerCluster ごとのクラスタを生成する。
		 * 法線は位置から面法線として計算する (頂点法線に依存しない)。
		 *
		 * outReorderedIndices には Morton ソート後の三角形順でインデックスを並べ直して出力する。
		 * クラスタ c の三角形は outReorderedIndices[triOffset*3 .. (triOffset+triCount)*3) に連続して並ぶ。
		 * これにより、可視クラスタの範囲を connect するだけで描画用インデックスを compact できる。
		 */
		void GenerateClusters(const std::vector<math::Vector3>& positions,
		                      const std::vector<uint32_t>& indices,
		                      uint32_t trisPerCluster,
		                      std::vector<MeshCluster>& outClusters,
		                      std::vector<uint32_t>& outReorderedIndices);


		/**
		 * クラスタがカメラから見えるか (フラスタム内 かつ バックフェースでない)。
		 * world = メッシュのワールド行列, frustum = カメラ視錐台, camPos = カメラ位置。
		 */
		inline bool IsClusterVisible(const MeshCluster& c, const math::Matrix4x4& world,
		                             const math::Frustum& frustum, const math::Vector3& camPos)
		{
			// フラスタムカリング (ワールド AABB)
			const math::AABB worldBox = math::AABB(c.center, c.extent).Transformed(world);
			if (!frustum.Intersects(worldBox)) return false;

			// バックフェースカリング (法線錐)
			DirectX::XMVECTOR axis = DirectX::XMVector3TransformNormal(c.coneAxis, world);
			axis = DirectX::XMVector3Normalize(axis);
			math::Vector3 viewDir(
				worldBox.center.x - camPos.x,
				worldBox.center.y - camPos.y,
				worldBox.center.z - camPos.z);
			DirectX::XMVECTOR vd = DirectX::XMVector3Normalize(viewDir);
			const float d = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vd, axis));
			if (d >= c.coneCutoff) return false;  // クラスタ全体が裏向き

			return true;
		}
	}
}
