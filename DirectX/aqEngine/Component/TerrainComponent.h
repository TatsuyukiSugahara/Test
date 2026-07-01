#pragma once
#include "ECS/ECS.h"
#include "Terrain/HeightmapChunk.h"
#include <string>
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif

namespace aq
{
	namespace ecs
	{
		class TerrainComponent : public IComponent
		{
			ecsComponent(aq::ecs::TerrainComponent);

		public:
			void SetDesc(const terrain::HeightmapChunk::Desc& desc);

			bool IsCompleted() const { return state_ == State::Completed; }

			terrain::HeightmapChunk* GetChunk() { return &chunk_; }
			const terrain::HeightmapChunk* GetChunk() const { return &chunk_; }

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				bool pathChanged = false;
				pathChanged |= visitor.FieldPath("Heightmap", heightmapPath_);
				pathChanged |= visitor.FieldPath("Splatmap",  splatmapPath_);
				pathChanged |= visitor.FieldPath("Layer 0",   layerPaths_[0]);
				pathChanged |= visitor.FieldPath("Layer 1",   layerPaths_[1]);
				pathChanged |= visitor.FieldPath("Layer 2",   layerPaths_[2]);
				if (pathChanged)
				{
					terrain::HeightmapChunk::Desc desc;
					desc.heightmapPath  = heightmapPath_.empty() ? nullptr : heightmapPath_.c_str();
					desc.splatmapPath   = splatmapPath_.empty()  ? nullptr : splatmapPath_.c_str();
					for (int i = 0; i < 3; ++i)
						desc.layerPaths[i] = layerPaths_[i].empty() ? nullptr : layerPaths_[i].c_str();
					desc.resolution  = chunk_.GetResolution();
					desc.heightScale = chunk_.GetHeightScale();
					desc.terrainSize = chunk_.GetTerrainSize();
					desc.layerTiling = layerTiling_;
					SetDesc(desc);
				}
				float hs = chunk_.GetHeightScale();
				if (ImGui::DragFloat("Height Scale", &hs, 0.1f, 0.1f, 500.0f))
					chunk_.SetHeightScale(hs);
				float ts = chunk_.GetTerrainSize();
				if (ImGui::DragFloat("Terrain Size", &ts, 1.0f, 1.0f, 10000.0f))
					chunk_.SetTerrainSize(ts);
				ImGui::DragFloat("Layer Tiling", &layerTiling_, 0.1f, 0.1f, 100.0f);
				visitor.ReadOnly("Resolution", static_cast<int>(chunk_.GetResolution()));
				visitor.ReadOnly("state", IsCompleted() ? "Completed" : "Invalid");
			}
#endif

			// 永続フィールドの列挙（JSON 保存/読込）。常時コンパイル。
			// heightScale/terrainSize は chunk_ の軽量セッターへ temp+apply で反映し、
			// resolution とパスはロード副作用（再構築）を OnDeserialized の SetDesc へ退避する。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("heightmap", heightmapPath_, "Heightmap");
				visitor.FieldPath("splatmap",  splatmapPath_,  "Splatmap");
				visitor.FieldPath("layer0",    layerPaths_[0], "Layer 0");
				visitor.FieldPath("layer1",    layerPaths_[1], "Layer 1");
				visitor.FieldPath("layer2",    layerPaths_[2], "Layer 2");
				visitor.Field("layerTiling", layerTiling_, "Layer Tiling");

				float hs = chunk_.GetHeightScale();
				visitor.Field("heightScale", hs, "Height Scale");
				chunk_.SetHeightScale(hs);
				float ts = chunk_.GetTerrainSize();
				visitor.Field("terrainSize", ts, "Terrain Size");
				chunk_.SetTerrainSize(ts);
				int res = static_cast<int>(chunk_.GetResolution());
				visitor.Field("resolution", res, "Resolution");
				authorResolution_ = static_cast<uint32_t>(res);
			}

			// deserialize 後に呼ぶ。読み込んだパス/パラメータで地形を再構築する。
			void OnDeserialized()
			{
				if (heightmapPath_.empty()) return;   // ハイトマップ未指定なら再構築しない
				terrain::HeightmapChunk::Desc desc;
				desc.heightmapPath = heightmapPath_.c_str();
				desc.splatmapPath  = splatmapPath_.empty() ? nullptr : splatmapPath_.c_str();
				for (int i = 0; i < 3; ++i)
					desc.layerPaths[i] = layerPaths_[i].empty() ? nullptr : layerPaths_[i].c_str();
				desc.resolution  = authorResolution_ ? authorResolution_ : chunk_.GetResolution();
				desc.heightScale = chunk_.GetHeightScale();
				desc.terrainSize = chunk_.GetTerrainSize();
				desc.layerTiling = layerTiling_;
				SetDesc(desc);
			}

		private:
			enum class State : uint8_t { Invalid, Completed };
			State state_ = State::Invalid;
			terrain::HeightmapChunk chunk_;
			std::string heightmapPath_;
			std::string splatmapPath_;
			std::string layerPaths_[3];
			float       layerTiling_      = 10.0f;
			uint32_t    authorResolution_ = 0;   // Reflect で捕獲したオーサリング解像度（OnDeserialized で使用）
		};
	}
}
