#pragma once
#ifdef AQ_DEBUG_IMGUI

#include <cstdint>

namespace aq
{
	namespace terrain
	{
		enum class TerrainPaintTool : uint8_t
		{
			Heightmap,
			Splatmap,
		};

		inline TerrainPaintTool& ActiveTerrainPaintTool()
		{
			static TerrainPaintTool tool = TerrainPaintTool::Heightmap;
			return tool;
		}
	}
}
#endif // AQ_DEBUG_IMGUI
