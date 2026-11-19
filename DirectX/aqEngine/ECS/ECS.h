#pragma once
#include "EntityContext.h"
#include "FieldAttr.h"   // Reflect の Field / FieldPath に渡すフィールド属性
#include <utility>


/**
 * 参考：https://qiita.com/harayuu10/items/e15b02e3b0f3081d729b
 */

namespace aq
{
	namespace ecs
	{
		template<typename... Cs, typename Func>
		void Foreach(Func&& func)
		{
			EntityContext::Get().GetView<Cs...>().ForEach(std::forward<Func>(func));
		}
	}
}
