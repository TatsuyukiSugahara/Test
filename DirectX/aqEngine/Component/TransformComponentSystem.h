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

			// 永続フィールドの列挙（ImGui 編集 / JSON 保存 / JSON 読込で共有）。
			// 第1引数=永続キー（JSON）、第3引数=UI 表示ラベル。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.Field("position", position, "position (local)");
				visitor.Field("scale",    scale,    "scale    (local)");
				visitor.Field("rotation", rotation, "rotation (local)");
			}
		};
	}
}
