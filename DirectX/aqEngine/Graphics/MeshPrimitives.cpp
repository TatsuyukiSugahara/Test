#include "aq.h"
#include "MeshPrimitives.h"


namespace aq
{
	namespace graphics
	{
		namespace
		{
			static constexpr float GRASS_TUFT_PI          = 3.14159265359f;
			static constexpr float GRASS_TUFT_TIP_TAPER   = 0.35f;   // 先端の幅 / 根元の幅 (先細り)
			static constexpr float GRASS_TUFT_NORMAL_BIAS = 0.4f;    // 面法線を上向きへ寄せる度合い (0=真上)
			static constexpr uint32_t GRASS_TUFT_QUAD_VERTEX_COUNT = 4;    // クアッド1枚あたりの頂点数
			static constexpr uint32_t GRASS_TUFT_QUAD_INDEX_COUNT  = 12;   // 同 インデックス数 (表2枚 + 裏2枚)
		}


		void BuildGrassTuftMesh(std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
		                        const uint32_t quadCount, const float height, const float width)
		{
			const uint32_t count        = (quadCount == 0) ? 1u : quadCount;
			const float    halfWidth    = width * 0.5f;
			const float    tipHalfWidth = halfWidth * GRASS_TUFT_TIP_TAPER;

			outVertices.clear();
			outVertices.reserve(static_cast<size_t>(count) * GRASS_TUFT_QUAD_VERTEX_COUNT);
			outIndices.clear();
			outIndices.reserve(static_cast<size_t>(count) * GRASS_TUFT_QUAD_INDEX_COUNT);

			for (uint32_t i = 0; i < count; ++i)
			{
				// 房を放射状に開く。範囲は 0..PI でよい (PI..2PI は同じ板を裏から見た向きになるだけ)。
				const float         angle = GRASS_TUFT_PI * static_cast<float>(i) / static_cast<float>(count);
				const math::Vector3 dir(cosf(angle), 0.0f, sinf(angle));

				// 面法線 (-dir.z, 0, dir.x) を上向きへ寄せる。両面から見て明暗が反転せず陰影が柔らかくなる。
				math::Vector3 normal(-dir.z * GRASS_TUFT_NORMAL_BIAS, 1.0f, dir.x * GRASS_TUFT_NORMAL_BIAS);
				normal.Normalize();

				// 根元 2 頂点 (uv.y=0) → 先端 2 頂点 (uv.y=1)。uv.y は葉の高さそのもの。
				VertexData vertex;
				vertex.normal.Set(normal);
				vertex.tangent.Set(0.0f, 0.0f, 0.0f, 0.0f);   // シェーダが使わないのでゼロのまま

				vertex.position.Set(-halfWidth * dir.x, 0.0f, -halfWidth * dir.z);
				vertex.uv.Set(0.0f, 0.0f);
				outVertices.push_back(vertex);

				vertex.position.Set(halfWidth * dir.x, 0.0f, halfWidth * dir.z);
				vertex.uv.Set(1.0f, 0.0f);
				outVertices.push_back(vertex);

				vertex.position.Set(tipHalfWidth * dir.x, height, tipHalfWidth * dir.z);
				vertex.uv.Set(1.0f, 1.0f);
				outVertices.push_back(vertex);

				vertex.position.Set(-tipHalfWidth * dir.x, height, -tipHalfWidth * dir.z);
				vertex.uv.Set(0.0f, 1.0f);
				outVertices.push_back(vertex);

				// 表面 2 枚 + 巻き順を反転した裏面 2 枚。頂点は共有するので頂点数は増えない。
				const uint32_t base = i * GRASS_TUFT_QUAD_VERTEX_COUNT;
				outIndices.push_back(base + 0);
				outIndices.push_back(base + 1);
				outIndices.push_back(base + 2);
				outIndices.push_back(base + 0);
				outIndices.push_back(base + 2);
				outIndices.push_back(base + 3);

				outIndices.push_back(base + 0);
				outIndices.push_back(base + 2);
				outIndices.push_back(base + 1);
				outIndices.push_back(base + 0);
				outIndices.push_back(base + 3);
				outIndices.push_back(base + 2);
			}
		}
	}
}
