#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "Graphics/StaticMesh.h"
#include "Graphics/IShaderResourceView.h"
#include "Rendering/RenderFrame.h"


namespace aq
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

			/** heightData_ を書き換えた後に呼ぶ: 法線再計算 + 動的VB更新 */
			void RebuildFromHeights();
			void RebuildSplatTexture();
			void FillSplatLayer(uint32_t layerIndex);

			void SetCastShadow(bool v)    { mesh_.SetCastShadow(v); }
			void SetReceiveShadow(bool v) { mesh_.SetReceiveShadow(v); }

			// HeightmapPainter 向け公開アクセサ
			float*   GetHeightDataMutable()  { return heightData_.data(); }
			const float* GetHeightData() const { return heightData_.data(); }
			uint32_t GetMapWidth()     const { return hmapWidth_; }
			uint32_t GetMapHeight()    const { return hmapHeight_; }
			float    GetTerrainSize()  const { return terrainSize_; }
			float    GetHeightScale()  const { return heightScale_; }
			uint32_t GetResolution()   const { return desc_.resolution; }

			math::Vector4*       GetSplatDataMutable()       { return splatData_.data(); }
			const math::Vector4* GetSplatData() const         { return splatData_.data(); }
			uint32_t GetSplatMapWidth()  const { return splatMapWidth_; }
			uint32_t GetSplatMapHeight() const { return splatMapHeight_; }
			graphics::IShaderResourceView* GetSplatTexture() const { return runtimeSplatSrv_.get(); }

			/** XZ スケール変更 + 頂点再計算 (height データは保持) */
			void SetTerrainSize(float size);
			/** Y スケール変更 + 頂点 Y 再計算 */
			void SetHeightScale(float scale);

		private:
			void BuildMesh(const std::vector<float>& heights,
			               uint32_t mapW, uint32_t mapH,
			               const Desc& desc);
			/** vertCache_ からローカル AABB を再計算しキャッシュする (カリング用) */
			void RecomputeBounds() const;

			graphics::StaticMesh mesh_;

			// カリング用ローカル AABB キャッシュ。頂点が変わったら boundsValid_=false にする。
			mutable math::AABB localBounds_;
			mutable bool       boundsValid_ = false;

			// CPU 側高さデータ (GetHeight + RebuildFromHeights で使用)
			std::vector<float>               heightData_;
			std::vector<graphics::VertexData> vertCache_;
			std::vector<math::Vector4>       splatData_;
			std::shared_ptr<graphics::IShaderResourceView> runtimeSplatSrv_;   // RebuildFromHeights 用キャッシュ
			uint32_t           hmapWidth_   = 0;
			uint32_t           hmapHeight_  = 0;
			uint32_t           splatMapWidth_  = 0;
			uint32_t           splatMapHeight_ = 0;
			float              terrainSize_ = 0.0f;
			float              heightScale_ = 0.0f;
			Desc               desc_        = {};            // RebuildFromHeights で参照
		};
	}
}
