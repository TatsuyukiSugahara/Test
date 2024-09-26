#pragma once
#include "TypeInfo.h"

/** �R���|�[�l���g�N���X�ɕK�v */
#define ecsComponent(T) TYPE_INFO(T)

namespace engine
{
	namespace ecs
	{
		/** �R���|�[�l���g�N���X�̊��N���X */
		struct IComponent
		{
		};


		/** �ő�R���|�[�l���g�� */
		constexpr uint32_t MAX_COMPONENT_SIZE = 16;


		template <class T>
		constexpr bool IsComponent = std::is_base_of_v<IComponent, T>&& std::is_trivial_v<T>&& std::is_trivially_destructible_v<T> && type::GetTypeName<T>;
	}
}