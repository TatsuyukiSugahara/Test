#pragma once
#include "ECS/ECS.h"
#include "Terrain/HeightmapChunk.h"
#ifdef AQ_DEBUG_IMGUI
#include <string>
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

		private:
			enum class State : uint8_t { Invalid, Completed };
			State state_ = State::Invalid;
			terrain::HeightmapChunk chunk_;
#ifdef AQ_DEBUG_IMGUI
			std::string heightmapPath_;
			std::string splatmapPath_;
			std::string layerPaths_[3];
			float       layerTiling_ = 10.0f;
#endif
		};
	}
}
