#include "aq.h"
#include "MeshPrimitives.h"


namespace aq
{
	namespace graphics
	{
		namespace
		{
			static constexpr float MESH_PI     = 3.14159265359f;
			static constexpr float MESH_TWO_PI = MESH_PI * 2.0f;

			/** 草の房 */
			static constexpr uint32_t GRASS_TUFT_SEGMENT_COUNT  = 3;      // 葉 1 枚を縦に分割する段数 (湾曲の滑らかさ)
			static constexpr float GRASS_TUFT_TIP_TAPER         = 0.15f;  // 先端の幅 / 根元の幅 (先細り)
			static constexpr float GRASS_TUFT_NORMAL_BIAS       = 0.4f;   // 面法線を上向きへ寄せる度合い (0=真上)
			static constexpr float GRASS_TUFT_AZIMUTH_JITTER    = 0.35f;  // 方位の均等割りを崩す量 [葉 1 枚ぶんの割り当て角に対する比]
			static constexpr float GRASS_TUFT_ROOT_OFFSET_MAX   = 0.06f;  // 葉の根元を房の中心からずらす距離の上限 [m]
			static constexpr float GRASS_TUFT_LEAN_ANGLE_MAX    = MESH_PI * 25.0f / 180.0f;  // 葉を倒す角度の上限 (25 度)
			static constexpr float GRASS_TUFT_HEIGHT_SCALE_MIN  = 0.55f;  // 葉ごとの高さ倍率の下限
			static constexpr float GRASS_TUFT_HEIGHT_SCALE_MAX  = 1.00f;  // 葉ごとの高さ倍率の上限
			static constexpr float GRASS_TUFT_WIDTH_SCALE_MIN   = 0.75f;  // 葉ごとの幅倍率の下限
			static constexpr float GRASS_TUFT_WIDTH_SCALE_MAX   = 1.25f;  // 葉ごとの幅倍率の上限
			static constexpr uint32_t GRASS_TUFT_BLADE_VERTEX_COUNT = (GRASS_TUFT_SEGMENT_COUNT + 1) * 2;  // 葉1枚あたりの頂点数 (段の境界ごとに左右 2 頂点)
			static constexpr uint32_t GRASS_TUFT_BLADE_INDEX_COUNT  = GRASS_TUFT_SEGMENT_COUNT * 12;       // 同 インデックス数 (1 段につき表2枚 + 裏2枚)

			/** 花 */
			static constexpr float FLOWER_PETAL_TILT             = MESH_PI * 35.0f / 180.0f;  // 花弁を水平から上向きへ開く角度 (35 度)
			static constexpr float FLOWER_CORE_RADIUS_RATIO      = 0.22f;  // 花芯の半径 / 花弁の先端までの半径
			static constexpr float FLOWER_PETAL_BASE_WIDTH_RATIO = 0.35f;  // 花弁の根元の幅 / 花弁の先端までの半径
			static constexpr float FLOWER_PETAL_TIP_WIDTH_RATIO  = 0.55f;  // 花弁の先端の幅 / 花弁の先端までの半径
			static constexpr float FLOWER_UV_Y_MIN               = 0.80f;  // 花弁の根元の uv.y (PS の縦グラデーションで暗く沈ませないための下限)
			static constexpr float FLOWER_UV_Y_MAX               = 1.00f;  // 花弁の先端と花芯の uv.y
			static constexpr uint32_t FLOWER_PETAL_VERTEX_COUNT = 4;    // 花弁1枚あたりの頂点数
			static constexpr uint32_t FLOWER_PETAL_INDEX_COUNT  = 12;   // 同 インデックス数 (表2枚 + 裏2枚)
			static constexpr uint32_t FLOWER_CORE_VERTEX_COUNT  = 4;    // 花芯 (四角1枚) の頂点数
			static constexpr uint32_t FLOWER_CORE_INDEX_COUNT   = 12;   // 同 インデックス数 (表2枚 + 裏2枚)

			/** ハッシュのチャンネル。同じ葉から独立した値を引くための第2シードで、値そのものに意味はない */
			static constexpr uint32_t HASH_CHANNEL_AZIMUTH      = 1;
			static constexpr uint32_t HASH_CHANNEL_ROOT_OFFSET  = 2;
			static constexpr uint32_t HASH_CHANNEL_LEAN_AZIMUTH = 3;
			static constexpr uint32_t HASH_CHANNEL_LEAN_ANGLE   = 4;
			static constexpr uint32_t HASH_CHANNEL_HEIGHT_SCALE = 5;
			static constexpr uint32_t HASH_CHANNEL_WIDTH_SCALE  = 6;


			/**
			 * インデックスから決まる [0,1] のハッシュ値。
			 * ワーカースレッドから呼ばれるうえ、同じ引数で常に同じメッシュになる必要があるので、
			 * グローバル状態を持つ乱数生成器は使わない。式は Terrain/SplatmapPainter.cpp の HashNoise と同じ。
			 * @param index   葉 / 花弁のインデックス
			 * @param channel 同じ index から独立した値を引くためのチャンネル
			 */
			float HashNoise(const uint32_t index, const uint32_t channel)
			{
				uint32_t h = index * 374761393u + channel * 668265263u;
				h = (h ^ (h >> 13)) * 1274126177u;
				h ^= h >> 16;
				return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
			}


			/** ハッシュ値を [minValue, maxValue] へ写したもの */
			float HashRange(const uint32_t index, const uint32_t channel, const float minValue, const float maxValue)
			{
				return minValue + (maxValue - minValue) * HashNoise(index, channel);
			}
		}


		void BuildGrassTuftMesh(std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
		                        const uint32_t bladeCount, const float height, const float width)
		{
			const uint32_t      count     = (bladeCount == 0) ? 1u : bladeCount;
			const float         halfWidth = width * 0.5f;
			const math::Vector3 up(0.0f, 1.0f, 0.0f);

			outVertices.clear();
			outVertices.reserve(static_cast<size_t>(count) * GRASS_TUFT_BLADE_VERTEX_COUNT);
			outIndices.clear();
			outIndices.reserve(static_cast<size_t>(count) * GRASS_TUFT_BLADE_INDEX_COUNT);

			for (uint32_t i = 0; i < count; ++i)
			{
				// 方位は均等割り (2PI * i / count) をハッシュで前後にずらす。均等なままだと
				// 房が放射状に対称になり、遠目に「緑の円錐」に見えてしまう。
				const float         jitter  = HashRange(i, HASH_CHANNEL_AZIMUTH, -GRASS_TUFT_AZIMUTH_JITTER, GRASS_TUFT_AZIMUTH_JITTER);
				const float         azimuth = MESH_TWO_PI * (static_cast<float>(i) + jitter) / static_cast<float>(count);
				const math::Vector3 azimuthDir(cosf(azimuth), 0.0f, sinf(azimuth));

				// 葉の根元を方位方向へずらす。1 点から生えていないので束ねた感じが薄れる。
				const float         rootOffset = HashRange(i, HASH_CHANNEL_ROOT_OFFSET, 0.0f, GRASS_TUFT_ROOT_OFFSET_MAX);
				const math::Vector3 rootPos    = azimuthDir * rootOffset;

				// 傾ける方位は葉の方位とは独立に引く。方位と揃えると再び放射状の対称形に戻ってしまう。
				const float         leanAzimuth = HashRange(i, HASH_CHANNEL_LEAN_AZIMUTH, 0.0f, MESH_TWO_PI);
				const math::Vector3 leanDir(cosf(leanAzimuth), 0.0f, sinf(leanAzimuth));
				const float         leanAngle   = HashRange(i, HASH_CHANNEL_LEAN_ANGLE, 0.0f, GRASS_TUFT_LEAN_ANGLE_MAX);

				const float bladeHeight    = height * HashRange(i, HASH_CHANNEL_HEIGHT_SCALE, GRASS_TUFT_HEIGHT_SCALE_MIN, GRASS_TUFT_HEIGHT_SCALE_MAX);
				const float bladeHalfWidth = halfWidth * HashRange(i, HASH_CHANNEL_WIDTH_SCALE, GRASS_TUFT_WIDTH_SCALE_MIN, GRASS_TUFT_WIDTH_SCALE_MAX);
				// 先端の水平変位 [m]。これを t^2 で効かせると、根元は立ったまま先端だけが垂れる葉になる。
				const float leanReach      = bladeHeight * tanf(leanAngle);

				// 葉の板は「上方向 × 傾き方向」を含む平面に立てる。幅はその平面の法線方向へ取る。
				math::Vector3 side;
				side.Cross(up, leanDir);
				side.Normalize();

				// 面法線 (葉が倒れている向き) を上向きへ寄せる。両面から見て明暗が反転せず陰影が柔らかくなる。
				math::Vector3 normal(leanDir.x * GRASS_TUFT_NORMAL_BIAS, 1.0f, leanDir.z * GRASS_TUFT_NORMAL_BIAS);
				normal.Normalize();

				VertexData vertex;
				vertex.normal.Set(normal);
				vertex.tangent.Set(0.0f, 0.0f, 0.0f, 0.0f);   // シェーダが使わないのでゼロのまま

				// 根元 (t=0) から先端 (t=1) へ段ごとに左右 2 頂点を積む。uv.y = t は葉の高さそのもので、
				// 風の重みと PS の縦グラデーションが両方これを見る。
				for (uint32_t s = 0; s <= GRASS_TUFT_SEGMENT_COUNT; ++s)
				{
					const float         t            = static_cast<float>(s) / static_cast<float>(GRASS_TUFT_SEGMENT_COUNT);
					const float         segHalfWidth = bladeHalfWidth * (1.0f - (1.0f - GRASS_TUFT_TIP_TAPER) * t);
					const math::Vector3 center       = rootPos + up * (bladeHeight * t) + leanDir * (leanReach * t * t);

					vertex.position.Set(center - side * segHalfWidth);
					vertex.uv.Set(0.0f, t);
					outVertices.push_back(vertex);

					vertex.position.Set(center + side * segHalfWidth);
					vertex.uv.Set(1.0f, t);
					outVertices.push_back(vertex);
				}

				// 段ごとに表面 2 枚 + 巻き順を反転した裏面 2 枚。頂点は共有するので頂点数は増えない。
				const uint32_t base = i * GRASS_TUFT_BLADE_VERTEX_COUNT;
				for (uint32_t s = 0; s < GRASS_TUFT_SEGMENT_COUNT; ++s)
				{
					const uint32_t v0 = base + s * 2 + 0;   // 下段 左
					const uint32_t v1 = base + s * 2 + 1;   // 下段 右
					const uint32_t v2 = base + s * 2 + 3;   // 上段 右
					const uint32_t v3 = base + s * 2 + 2;   // 上段 左

					outIndices.push_back(v0);
					outIndices.push_back(v1);
					outIndices.push_back(v2);
					outIndices.push_back(v0);
					outIndices.push_back(v2);
					outIndices.push_back(v3);

					outIndices.push_back(v0);
					outIndices.push_back(v2);
					outIndices.push_back(v1);
					outIndices.push_back(v0);
					outIndices.push_back(v3);
					outIndices.push_back(v2);
				}
			}
		}


		void BuildFlowerMesh(std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
		                     const uint32_t petalCount, const float height, const float radius)
		{
			const uint32_t count         = (petalCount == 0) ? 1u : petalCount;
			const float    coreRadius    = radius * FLOWER_CORE_RADIUS_RATIO;
			const float    baseHalfWidth = radius * FLOWER_PETAL_BASE_WIDTH_RATIO * 0.5f;
			const float    tipHalfWidth  = radius * FLOWER_PETAL_TIP_WIDTH_RATIO * 0.5f;
			const float    tiltCos       = cosf(FLOWER_PETAL_TILT);
			const float    tiltSin       = sinf(FLOWER_PETAL_TILT);

			const math::Vector3 center(0.0f, height, 0.0f);

			outVertices.clear();
			outVertices.reserve(static_cast<size_t>(count) * FLOWER_PETAL_VERTEX_COUNT + FLOWER_CORE_VERTEX_COUNT);
			outIndices.clear();
			outIndices.reserve(static_cast<size_t>(count) * FLOWER_PETAL_INDEX_COUNT + FLOWER_CORE_INDEX_COUNT);

			VertexData vertex;
			vertex.tangent.Set(0.0f, 0.0f, 0.0f, 0.0f);   // シェーダが使わないのでゼロのまま

			for (uint32_t i = 0; i < count; ++i)
			{
				// 花弁を放射状に並べ、水平から FLOWER_PETAL_TILT だけ上向きへ開く。
				const float         angle = MESH_TWO_PI * static_cast<float>(i) / static_cast<float>(count);
				const math::Vector3 petalDir(cosf(angle) * tiltCos, tiltSin, sinf(angle) * tiltCos);
				const math::Vector3 side(-sinf(angle), 0.0f, cosf(angle));   // 花弁の幅方向 (水平)

				// 面法線は花弁の面に垂直 (幅方向 × 伸びる方向)。上向き成分が残るので上から見て明るくなる。
				math::Vector3 normal;
				normal.Cross(side, petalDir);
				normal.Normalize();
				vertex.normal.Set(normal);

				const math::Vector3 basePos = center + petalDir * coreRadius;   // 花芯との境目
				const math::Vector3 tipPos  = center + petalDir * radius;       // 花弁の先端

				// uv.y は 0.80〜1.00。InstancedGrass の PS が uv.y で縦グラデーションを掛けるので、
				// 0 に近い値にすると花がほとんど暗く沈んでしまう (花冠には接地の影が要らない)。
				vertex.position.Set(basePos - side * baseHalfWidth);
				vertex.uv.Set(0.0f, FLOWER_UV_Y_MIN);
				outVertices.push_back(vertex);

				vertex.position.Set(basePos + side * baseHalfWidth);
				vertex.uv.Set(1.0f, FLOWER_UV_Y_MIN);
				outVertices.push_back(vertex);

				vertex.position.Set(tipPos + side * tipHalfWidth);
				vertex.uv.Set(1.0f, FLOWER_UV_Y_MAX);
				outVertices.push_back(vertex);

				vertex.position.Set(tipPos - side * tipHalfWidth);
				vertex.uv.Set(0.0f, FLOWER_UV_Y_MAX);
				outVertices.push_back(vertex);

				// 表面 2 枚 + 巻き順を反転した裏面 2 枚 (草の房と同じく背面カリング対策で両面にする)。
				const uint32_t base = i * FLOWER_PETAL_VERTEX_COUNT;
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

			// 花芯。花弁の根元が集まる中心を塞ぐ水平な四角 1 枚。
			{
				vertex.normal.Set(0.0f, 1.0f, 0.0f);

				// 花芯は花の一番明るい所なので uv.y は 4 頂点とも上限で焼く。
				vertex.position.Set(-coreRadius, height, -coreRadius);
				vertex.uv.Set(0.0f, FLOWER_UV_Y_MAX);
				outVertices.push_back(vertex);

				vertex.position.Set(coreRadius, height, -coreRadius);
				vertex.uv.Set(1.0f, FLOWER_UV_Y_MAX);
				outVertices.push_back(vertex);

				vertex.position.Set(coreRadius, height, coreRadius);
				vertex.uv.Set(1.0f, FLOWER_UV_Y_MAX);
				outVertices.push_back(vertex);

				vertex.position.Set(-coreRadius, height, coreRadius);
				vertex.uv.Set(0.0f, FLOWER_UV_Y_MAX);
				outVertices.push_back(vertex);

				const uint32_t base = count * FLOWER_PETAL_VERTEX_COUNT;
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
