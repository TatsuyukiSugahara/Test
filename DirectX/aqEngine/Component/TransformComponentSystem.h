#pragma once
#include "ECS/ECS.h"

namespace aq
{
	namespace ecs
	{
		// ワールド座標をまとめて持つ構造体。HierarchicalTransformComponent で使用する。
		struct Transform
		{
			aq::math::Vector3    position = aq::math::Vector3::Zero;
			aq::math::Vector3    scale    = aq::math::Vector3::One;
			aq::math::Quaternion rotation = aq::math::Quaternion::Identity;
		};


		// ローカル座標のみを保持する。階層関係は持たない。
		// HierarchicalTransformComponent と常にセットで生成すること。
		struct TransformComponent : public IComponent
		{
			ecsComponent(aq::ecs::TransformComponent);

			aq::math::Vector3    position = aq::math::Vector3::Zero;
			aq::math::Vector3    scale    = aq::math::Vector3::One;
			aq::math::Quaternion rotation = aq::math::Quaternion::Identity;

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				visitor.Field("position (local)", position);
				visitor.Field("scale    (local)", scale);
				visitor.Field("rotation (local)", rotation);
			}
#endif
		};
	}
}
