#include "aq.h"
#include "TerrainComponent.h"

namespace aq
{
	namespace ecs
	{
		void TerrainComponent::SetDesc(const terrain::HeightmapChunk::Desc& desc)
		{
			SetDesc(desc, terrain::HeightmapChunk::PrepareCpuData(desc));
		}


		void TerrainComponent::SetDesc(const terrain::HeightmapChunk::Desc& desc, terrain::HeightmapChunk::CpuData&& cpu)
		{
			chunk_.Initialize(desc, std::move(cpu));
			state_ = State::Completed;
			// オーサリング値を実際に作った地形の値に揃える（後の書き出しが古い値を書かないように）
			authorResolution_  = desc.resolution;
			authorHeightScale_ = desc.heightScale;
			authorTerrainSize_ = desc.terrainSize;
#ifdef AQ_DEBUG_IMGUI
			heightmapPath_ = desc.heightmapPath ? desc.heightmapPath : "";
			splatmapPath_  = desc.splatmapPath  ? desc.splatmapPath  : "";
			for (int i = 0; i < 3; ++i)
				layerPaths_[i] = desc.layerPaths[i] ? desc.layerPaths[i] : "";
			layerTiling_ = desc.layerTiling;
#endif
		}
	}
}
