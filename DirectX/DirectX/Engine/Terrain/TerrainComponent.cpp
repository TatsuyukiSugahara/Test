#include "EnginePreCompile.h"
#include "TerrainComponent.h"

namespace engine
{
	namespace ecs
	{
		void TerrainComponent::SetDesc(const terrain::HeightmapChunk::Desc& desc)
		{
			chunk_.Initialize(desc);
			state_ = State::Completed;
		}
	}
}
