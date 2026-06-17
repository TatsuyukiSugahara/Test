#include "aq.h"
#include "Engine.h"
#include "TransformComponentSystem.h"

namespace aq
{
	namespace ecs
	{
		Transform::Transform()
			: position(aq::math::Vector3::Zero)
			, localPosition(aq::math::Vector3::Zero)
			, scale(aq::math::Vector3::One)
			, localScale(aq::math::Vector3::One)
			, angle(aq::math::Vector3::Zero)
			, localAngle(aq::math::Vector3::Zero)
			, rotation(aq::math::Quaternion::Identity)
			, localRotation(aq::math::Quaternion::Identity)
			, rotationMatrix(aq::math::Matrix4x4::Identity)
			, worldMatrix(aq::math::Matrix4x4::Identity)
			, parent(nullptr)
		{
			children.clear();
		}


		Transform::~Transform()
		{
			if (parent) {
				parent->RemoveChild(this);
			}
			Release();
		}


		void Transform::UpdateTransform()
		{
			if (parent) {
				// 座標計算
				aq::math::Matrix4x4 localPos;
				localPos.MakeTranslation(localPosition);

				aq::math::Matrix4x4 pos;
				pos.Mull(localPos, parent->worldMatrix);

				position.x = pos.m[0][3];
				position.y = pos.m[1][3];
				position.z = pos.m[2][3];

				// スケール
				scale = localScale * parent->scale;

				// 回転
				angle = localAngle + parent->angle;
				rotation.SetEuler(angle);
			} else {
				position = localPosition;
				scale = localScale;
				angle = localAngle;
				rotation.SetEuler(angle);
			}
			// 回転行列
			rotationMatrix.MakeRotationFromQuaternion(rotation);
			// ワールド行列更新
			UpdateWorldMatrix();
		}


		void Transform::UpdateWorldMatrix()
		{
			aq::math::Matrix4x4 scal, pos, world;
			scal.MakeScaling(scale);
			pos.MakeTranslation(position);

			world.Mull(scal, rotationMatrix);
			worldMatrix.Mull(world, pos);

			// 子も更新
			for (Transform* child : children) {
				child->UpdateTransform();
			}
		}


		void Transform::Release()
		{
			std::vector<Transform*>::iterator it = children.begin();
			while (it != children.end())
			{
				(*it)->parent = nullptr;
				children.erase(it);
				++it;
			}
			children.clear();
		}


		void Transform::RemoveChild(Transform* t)
		{
			std::vector<Transform*>::iterator it = children.begin();
			while (it != children.end())
			{
				Transform* child = (*it);
				if (child == t) {
					child->parent = nullptr;
					children.erase(it);
					return;
				}
				++it;
			}
		}




		/*******************************************/


		TransformComponent::TransformComponent()
		{
		}


		TransformComponent::~TransformComponent()
		{
		}




		/*******************************************/


		HierarcicalTransformSystem* HierarcicalTransformSystem::instance_ = nullptr;


		HierarcicalTransformSystem::HierarcicalTransformSystem()
		{
			instance_ = this;
		}


		HierarcicalTransformSystem::~HierarcicalTransformSystem()
		{
			instance_ = nullptr;
		}


		void HierarcicalTransformSystem::Update()
		{
			aq::ecs::Foreach<TransformComponent>([](const aq::ecs::Entity& entity, TransformComponent* component)
				{
					component->transform.UpdateTransform();
				});
		}
	}
}