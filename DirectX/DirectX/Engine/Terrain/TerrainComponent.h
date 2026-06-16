#pragma once
#include "ECS/ECS.h"
#include "Terrain/HeightmapChunk.h"

namespace engine
{
	namespace ecs
	{
		class TerrainComponent : public IComponent
		{
			ecsComponent(engine::ecs::TerrainComponent);

		public:
			void SetDesc(const terrain::HeightmapChunk::Desc& desc);

			bool IsCompleted() const { return state_ == State::Completed; }

			terrain::HeightmapChunk* GetChunk() { return &chunk_; }
			const terrain::HeightmapChunk* GetChunk() const { return &chunk_; }

		private:
			enum class State : uint8_t { Invalid, Completed };
			State state_ = State::Invalid;
			terrain::HeightmapChunk chunk_;
		};
	}
}
