#pragma once
#include "TypeInfo.h"

#define ecsComponent(T) TYPE_INFO(T)

/** コンポーネントクラスに必要 */
namespace aq
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
		constexpr bool IsComponent = std::is_base_of_v<IComponent, T> && type::GetTypeName<T>;
	}
}
