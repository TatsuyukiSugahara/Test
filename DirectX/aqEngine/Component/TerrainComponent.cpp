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
