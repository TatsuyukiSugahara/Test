#pragma once
#include "EntityManager.h"
#include "System.h"
#include <utility>


/**
 * 参考：https://qiita.com/harayuu10/items/e15b02e3b0f3081d729b
 */

namespace engine
{
	namespace ecs
	{
		template<typename... Cs, typename Func>
		void Foreach(Func&& func)
		{
			EntityManager::Get().GetView<Cs...>().ForEach(std::forward<Func>(func));
		}
	}
}
