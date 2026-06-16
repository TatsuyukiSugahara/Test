#include "EnginePreCompile.h"
#include "ActionMap.h"

namespace engine
{
	namespace hid
	{
		const std::vector<InputBinding>* ActionMap::Find(uint32_t actionId) const
		{
			const auto it = map_.find(actionId);
			return (it != map_.end()) ? &it->second : nullptr;
		}

		const StickBinding* ActionMap::FindStick(uint32_t actionId) const
		{
			const auto it = stickMap_.find(actionId);
			return (it != stickMap_.end()) ? &it->second : nullptr;
		}
	}
}
