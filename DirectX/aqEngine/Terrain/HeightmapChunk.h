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

			/**
			 * ワーカースレッドで前計算できる CPU 側データ(GPU リソースを含まない)。
			 * PrepareCpuData(desc) で作り、Initialize(desc, std::move(cpu)) に渡すと
			 * メインスレッドの仕事は VB/IB とスプラットテクスチャの生成だけになる。
			 * 実測(Debug)では画像デコード+頂点生成+画素変換で 200ms 超がメインスレッドから消える。
			 */
			struct CpuData
			{
				std::vector<float>                heights;       // ハイトマップ R チャンネル [0,1]
				uint32_t                          hmapW = 0;
				uint32_t                          hmapH = 0;
				std::vector<math::Vector4>        splat;         // レイヤー重み(正規化済み)
				uint32_t                          splatW = 0;
				uint32_t                          splatH = 0;
				std::vector<graphics::VertexData> vertices;      // ComputeVertices の結果
				std::vector<uint32_t>             indices;
				std::vector<uint8_t>              splatPixels;   // RGBA8 化したスプラットマップ
			};

			/** desc から CpuData を作る。ファイル I/O と CPU 計算のみでスレッド安全(GPU/ECS に触れない) */
			static CpuData PrepareCpuData(const Desc& desc);

			/** CpuData から地形ローカル XZ の高さを取る (チャンク生成前・ワーカースレッドから使える) */
			static float SampleHeight(const CpuData& cpu, const Desc& desc, const float localX, const float localZ);

			/** CpuData から地形ローカル XZ のレイヤー重みを取る (x=layer0/草, y=layer1, z=layer2) */
			static math::Vector4 SampleSplat(const CpuData& cpu, const Desc& desc, const float localX, const float localZ);

			/** PrepareCpuData + Initialize(desc, cpu) を同期で行う(従来の入口) */
			void Initialize(const Desc& desc);
			/** 前計算済みデータから GPU リソースだけを生成する(メインスレッド) */
			void Initialize(const Desc& desc, CpuData&& cpu);
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
			/** vertCache_ と indices から動的 VB/IB を生成する */
			void UploadMesh(const std::vector<uint32_t>& indices);
			/** RGBA8 画素列からスプラットテクスチャを生成してマテリアルに差す */
			void UploadSplatTexture(const std::vector<uint8_t>& pixels);
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
