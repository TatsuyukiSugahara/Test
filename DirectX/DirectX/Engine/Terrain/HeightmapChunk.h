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
		 *
		 * 使い方:
		 *   HeightmapChunk::Desc desc;
		 *   desc.heightmapPath = "Assets/Terrain/heightmap.png";  // Rチャンネルが高さ
		 *   desc.albedoPath    = "Assets/Terrain/grass.dds";
		 *   desc.resolution    = 128;
		 *   desc.heightScale   = 20.0f;
		 *   desc.terrainSize   = 200.0f;
		 *   desc.uvTiling      = 20.0f;
		 *   chunk.Initialize(desc);
		 *
		 *   // 毎フレーム
		 *   chunk.Update(pos, rot, scale);
		 *   rendering::RenderItem item;
		 *   if (chunk.FillRenderItem(item)) { frame.items.push_back(item); }
		 */
		class HeightmapChunk
		{
		public:
			struct Desc
			{
				const char* heightmapPath = nullptr;  // Rチャンネルが高さ (PNG/DDS等)
				const char* albedoPath    = nullptr;  // 地面テクスチャ (ResourceManager経由)
				uint32_t    resolution    = 128;      // グリッド分割数 (頂点数 = (N+1)^2)
				float       heightScale   = 10.0f;    // R=1.0 のときの最大高さ (m)
				float       terrainSize   = 100.0f;   // XZ 一辺の長さ (m)
				float       uvTiling      = 10.0f;    // テクスチャのタイリング回数
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
