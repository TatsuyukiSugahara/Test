#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "TransformComponent.h"

namespace engine
{
	namespace ecs
	{
		Transform::Transform()
			: position(engine::math::Vector3::Zero)
			, localPosition(engine::math::Vector3::Zero)
			, scale(engine::math::Vector3::One)
			, localScale(engine::math::Vector3::One)
			, angle(engine::math::Vector3::Zero)
			, localAngle(engine::math::Vector3::Zero)
			, rotation(engine::math::Quaternion::Identity)
			, localRotation(engine::math::Quaternion::Identity)
			, rotationMatrix(engine::math::Matrix4x4::Identity)
			, worldMatrix(engine::math::Matrix4x4::Identity)
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
				// ���W�v�Z
				engine::math::Matrix4x4 localPos;
				localPos.MakeTranslation(localPosition);

				engine::math::Matrix4x4 pos;
				pos.Mull(localPos, parent->worldMatrix);

				position.x = pos.m[0][3];
				position.y = pos.m[1][3];
				position.z = pos.m[2][3];

				// �X�P�[��
				scale = localScale * parent->scale;

				// ��]
				angle = localAngle + parent->angle;
				rotation.SetEuler(angle);
			} else {
				position = localPosition;
				scale = localScale;
				angle = localAngle;
				rotation.SetEuler(angle);
			}
			// ��]�s��
			rotationMatrix.MakeRotationFromQuaternion(rotation);
			// ���[���h�s��X�V
			UpdateWorldMatrix();
		}


		void Transform::UpdateWorldMatrix()
		{
			engine::math::Matrix4x4 scal, pos, world;
			scal.MakeScaling(scale);
			pos.MakeTranslation(position);

			world.Mull(scal, rotationMatrix);
			worldMatrix.Mull(world, pos);

			// �q���X�V
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
	}
}