#pragma once
#include <cstdint>
#include <vector>
#include "Graphics/StaticMesh.h"
#include "Rendering/RenderFrame.h"


namespace engine
{
	namespace terrain
	{
		/**
		 * ハイトマップから生成される地形メッシュ。
		 * ECS からは TerrainComponent 経由で使う。
		 *
		 * 使い方:
		 *   HeightmapChunk::Desc desc;
		 *   desc.heightmapPath  = "Assets/Terrain/heightmap.png"; // R=高さ
		 *   desc.splatmapPath   = "Assets/Terrain/splatmap.png";  // R=layer0, G=layer1, B=layer2
		 *   desc.layerPaths[0]  = "Assets/Terrain/grass.dds";
		 *   desc.layerPaths[1]  = "Assets/Terrain/rock.dds";
		 *   desc.layerPaths[2]  = "Assets/Terrain/dirt.dds";
		 *   desc.resolution     = 128;
		 *   desc.heightScale    = 10.0f;
		 *   desc.terrainSize    = 100.0f;
		 *   desc.layerTiling    = 20.0f;
		 *   chunk.Initialize(desc);
		 *
		 * splatmapPath を null にすると layer0 のみで描画する。
		 * 必要なアセット: heightmap.png / splatmap.png / grass.dds / rock.dds / dirt.dds
		 */
		class HeightmapChunk
		{
		public:
			struct Desc
			{
				const char* heightmapPath = nullptr;   // Rチャンネルが高さ (PNG/DDS等)
				// スプラットマップ: R=layer0, G=layer1, B=layer2 の混合比率
				// null のときは layer0 のみ使用
				const char* splatmapPath  = nullptr;
				// レイヤーテクスチャ (grass/rock/dirt など、最大3枚)
				const char* layerPaths[3] = {};
				uint32_t    resolution    = 128;       // グリッド分割数
				float       heightScale   = 10.0f;     // R=1.0 のときの最大高さ (m)
				float       terrainSize   = 100.0f;    // XZ 一辺の長さ (m)
				float       layerTiling   = 10.0f;     // レイヤーテクスチャのUV倍率
			};

			void Initialize(const Desc& desc);
			void Update(const math::Vector3& position,
			            const math::Quaternion& rotation,
			            const math::Vector3& scale);
			bool FillRenderItem(rendering::RenderItem& item) const;

			/** ワールド XZ 座標から地面の高さを返す (バイリニア補間) */
			float GetHeight(float worldX, float worldZ) const;

			void SetCastShadow(bool v)    { mesh_.SetCastShadow(v); }
			void SetReceiveShadow(bool v) { mesh_.SetReceiveShadow(v); }

		private:
			void BuildMesh(const std::vector<float>& heights,
			               uint32_t mapW, uint32_t mapH,
			               const Desc& desc);

			graphics::StaticMesh mesh_;

			// GetHeight() 用に CPU へ保持する高さデータ
			std::vector<float> heightData_;
			uint32_t           hmapWidth_   = 0;
			uint32_t           hmapHeight_  = 0;
			float              terrainSize_ = 0.0f;
			float              heightScale_ = 0.0f;
		};
	}
}
