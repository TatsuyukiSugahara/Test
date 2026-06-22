#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "IComponent.h"
#include <cstring>

namespace aq
{
	namespace ecs
	{
		// デバッグビルドのみ存在するマーカーコンポーネント。
		// EntityContext::CreateEntity で全 Entity に自動注入される。
		// Hierarchy UI での名前表示と全 Entity 列挙（Foreach<EntityDebugTag>）に使用する。
		struct EntityDebugTag : public IComponent
		{
			ecsComponent(aq::ecs::EntityDebugTag);

			char displayName[32] = {};

			void SetName(const char* name)
			{
				strncpy_s(displayName, sizeof(displayName), name, _TRUNCATE);
			}
		};
	}
}
#endif
