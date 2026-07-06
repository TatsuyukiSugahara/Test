#include "aq.h"
#include "Meshlet.h"
#include <cmath>
#include <cfloat>


namespace aq
{
	namespace graphics
	{
		namespace
		{
			// 10bit を 3D Morton 用に展開する (1ビットおきに2つ空ける)
			uint32_t Part1By2(uint32_t x)
			{
				x &= 0x3ffu;
				x = (x | (x << 16)) & 0x030000FFu;
				x = (x | (x <<  8)) & 0x0300F00Fu;
				x = (x | (x <<  4)) & 0x030C30C3u;
				x = (x | (x <<  2)) & 0x09249249u;
				return x;
			}
			uint32_t Morton3D(uint32_t x, uint32_t y, uint32_t z)
			{
				return Part1By2(x) | (Part1By2(y) << 1) | (Part1By2(z) << 2);
			}
		}


		void GpuClusterBuffers::Create(const std::vector<MeshCluster>& clustersIn,
		                               const std::vector<uint32_t>& reorderedIndices)
		{
			clusterCount = 0;
			if (clustersIn.empty() || reorderedIndices.empty()) return;

			auto& gd = GraphicsDevice::Get();
			const uint32_t idxBytes = static_cast<uint32_t>(reorderedIndices.size()) * 4u;

			clusters   = gd.CreateStructuredBuffer(
				static_cast<uint32_t>(sizeof(MeshCluster)),
				static_cast<uint32_t>(clustersIn.size()), clustersIn.data());
			srcIndices = gd.CreateRawBuffer(idxBytes, /*srv*/true,  /*uav*/false, reorderedIndices.data());
			outIndices = gd.CreateRawBuffer(idxBytes, /*srv*/false, /*uav*/true,  nullptr);
			args       = gd.CreateRawBuffer(20u,      /*srv*/false, /*uav*/true,  nullptr);  // DRAW_INDEXED_ARGS

			if (clusters && srcIndices && outIndices && args)
				clusterCount = static_cast<uint32_t>(clustersIn.size());
		}


		void GenerateClusters(const std::vector<math::Vector3>& positions,
		                      const std::vector<uint32_t>& indices,
		                      uint32_t trisPerCluster,
		                      std::vector<MeshCluster>& outClusters,
		                      std::vector<uint32_t>& outReorderedIndices)
		{
			outClusters.clear();
			outReorderedIndices.clear();
			if (trisPerCluster == 0 || indices.size() < 3) return;

			const uint32_t triTotal  = static_cast<uint32_t>(indices.size() / 3);
			const uint32_t vertCount = static_cast<uint32_t>(positions.size());

			// メッシュ全体の AABB (Morton 量子化の基準)
			math::AABBBuilder meshBounds;
			for (const math::Vector3& p : positions) meshBounds.Add(p);
			if (!meshBounds.HasPoint()) return;
			const math::AABB mesh = meshBounds.Build();
			const math::Vector3 minP(mesh.center.x - mesh.extent.x,
			                         mesh.center.y - mesh.extent.y,
			                         mesh.center.z - mesh.extent.z);
			const float invSx = (mesh.extent.x > 1e-6f) ? (1023.0f / (2.0f * mesh.extent.x)) : 0.0f;
			const float invSy = (mesh.extent.y > 1e-6f) ? (1023.0f / (2.0f * mesh.extent.y)) : 0.0f;
			const float invSz = (mesh.extent.z > 1e-6f) ? (1023.0f / (2.0f * mesh.extent.z)) : 0.0f;

			// 三角形を重心の Morton コードで空間ソート → 近い三角形が同じクラスタへ
			struct TriKey { uint32_t code; uint32_t tri; };
			std::vector<TriKey> order;
			order.reserve(triTotal);
			for (uint32_t t = 0; t < triTotal; ++t)
			{
				const uint32_t i0 = indices[3u * t + 0];
				const uint32_t i1 = indices[3u * t + 1];
				const uint32_t i2 = indices[3u * t + 2];
				if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) continue;
				const math::Vector3& p0 = positions[i0];
				const math::Vector3& p1 = positions[i1];
				const math::Vector3& p2 = positions[i2];
				const float cx = (p0.x + p1.x + p2.x) / 3.0f;
				const float cy = (p0.y + p1.y + p2.y) / 3.0f;
				const float cz = (p0.z + p1.z + p2.z) / 3.0f;
				auto q = [](float v, float mn, float inv) -> uint32_t
				{
					int g = static_cast<int>((v - mn) * inv);
					if (g < 0) g = 0; if (g > 1023) g = 1023;
					return static_cast<uint32_t>(g);
				};
				TriKey k;
				k.code = Morton3D(q(cx, minP.x, invSx), q(cy, minP.y, invSy), q(cz, minP.z, invSz));
				k.tri  = t;
				order.push_back(k);
			}
			std::sort(order.begin(), order.end(),
				[](const TriKey& a, const TriKey& b) { return a.code < b.code; });

			// ソート後の三角形順でインデックスを並べ直す (クラスタが連続範囲になる)
			outReorderedIndices.reserve(order.size() * 3u);
			for (const TriKey& k : order)
			{
				const uint32_t t = k.tri;
				outReorderedIndices.push_back(indices[3u * t + 0]);
				outReorderedIndices.push_back(indices[3u * t + 1]);
				outReorderedIndices.push_back(indices[3u * t + 2]);
			}

			const uint32_t usable      = static_cast<uint32_t>(order.size());
			const uint32_t clusterCount = (usable + trisPerCluster - 1) / trisPerCluster;
			outClusters.reserve(clusterCount);

			// クラスタ内の k 番目 (ソート後順序) の三角形の面法線を計算する。
			// 縮退三角形なら false。std::vector<XMVECTOR> に法線を貯めると 16byte アラインが
			// 必要な要素を std::vector に持つことになり環境によって不安定なため、保持せず 2 パスで再計算する。
			const auto faceNormal = [&](uint32_t k, DirectX::XMVECTOR& outN) -> bool
			{
				const uint32_t t  = order[k].tri;
				const math::Vector3& p0 = positions[indices[3u * t + 0]];
				const math::Vector3& p1 = positions[indices[3u * t + 1]];
				const math::Vector3& p2 = positions[indices[3u * t + 2]];
				const DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract(p1, p0);
				const DirectX::XMVECTOR e2 = DirectX::XMVectorSubtract(p2, p0);
				const DirectX::XMVECTOR n  = DirectX::XMVector3Cross(e1, e2);
				if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n)) <= 1e-12f) return false;
				outN = DirectX::XMVector3Normalize(n);
				return true;
			};

			for (uint32_t c = 0; c < clusterCount; ++c)
			{
				const uint32_t k0 = c * trisPerCluster;
				const uint32_t k1 = (k0 + trisPerCluster < usable) ? (k0 + trisPerCluster) : usable;

				// パス1: AABB + 平均法線 (有効な面法線数もカウント)
				math::AABBBuilder bounds;
				DirectX::XMVECTOR normalSum        = DirectX::XMVectorZero();
				uint32_t          validNormalCount = 0;

				for (uint32_t k = k0; k < k1; ++k)
				{
					const uint32_t t  = order[k].tri;
					bounds.Add(positions[indices[3u * t + 0]]);
					bounds.Add(positions[indices[3u * t + 1]]);
					bounds.Add(positions[indices[3u * t + 2]]);

					DirectX::XMVECTOR n;
					if (faceNormal(k, n))
					{
						normalSum = DirectX::XMVectorAdd(normalSum, n);
						++validNormalCount;
					}
				}

				if (!bounds.HasPoint()) continue;

				MeshCluster cluster;
				const math::AABB box = bounds.Build();
				cluster.center    = box.center;
				cluster.extent    = box.extent;
				cluster.triOffset = k0;          // ソート後順序での先頭 (描画適用時に再順序 IB と対応)
				cluster.triCount  = k1 - k0;

				if (validNormalCount > 0 &&
				    DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(normalSum)) > 1e-12f)
				{
					const DirectX::XMVECTOR axis = DirectX::XMVector3Normalize(normalSum);
					DirectX::XMStoreFloat3(&cluster.coneAxis.vector, axis);

					// パス2: 錐の開き角 (最小 dot)。面法線を再計算して求める。
					float mindp = 1.0f;
					for (uint32_t k = k0; k < k1; ++k)
					{
						DirectX::XMVECTOR n;
						if (!faceNormal(k, n)) continue;
						const float dp = DirectX::XMVectorGetX(DirectX::XMVector3Dot(n, axis));
						if (dp < mindp) mindp = dp;
					}
					cluster.coneCutoff = (mindp <= 0.0f) ? 2.0f : std::sqrt(1.0f - mindp * mindp);
				}
				else
				{
					cluster.coneAxis.Set(0.0f, 0.0f, 0.0f);
					cluster.coneCutoff = 2.0f;
				}

				outClusters.push_back(cluster);
			}
		}
	}
}
