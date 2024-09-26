#pragma once
#include "TypeInfo.h"

/** コンポーネントクラスに必要 */
#define ecsComponent(T) TYPE_INFO(T)

namespace engine
{
	namespace ecs
	{
		/** コンポーネントクラスの基底クラス */
		struct IComponent
		{
		};


		/** 最大コンポーネント数 */
		constexpr uint32_t MAX_COMPONENT_SIZE = 16;


		template <class T>
		constexpr bool IsComponent = std::is_base_of_v<IComponent, T>&& std::is_trivial_v<T>&& std::is_trivially_destructible_v<T> && type::GetTypeName<T>;
	}
}