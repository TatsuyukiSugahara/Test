#pragma once
#include "ECS/ECS.h"

namespace aq
{
	namespace ecs
	{
		struct Transform
		{
		public:
			aq::math::Vector3 position;
			aq::math::Vector3 localPosition;
			aq::math::Vector3 scale;
			aq::math::Vector3 localScale;
			aq::math::Vector3 angle;
			aq::math::Vector3 localAngle;
			aq::math::Quaternion rotation;
			aq::math::Quaternion localRotation;

			aq::math::Matrix4x4 rotationMatrix;
			aq::math::Matrix4x4 worldMatrix;

			Transform* parent;
			std::vector<Transform*> children;


		public:
			Transform();
			~Transform();

			void UpdateTransform();
			void UpdateWorldMatrix();

			void Release();
			void RemoveChild(Transform* t);

			void SetParent(Transform* p)
			{
				parent = p;
				parent->children.push_back(this);
			}
		};


		struct TransformComponent : public IComponent
		{
			ecsComponent(aq::ecs::TransformComponent);


		public:
			Transform transform;


		public:
			TransformComponent();
			~TransformComponent();


		public:
			void SetParent(TransformComponent* parent)
			{
				transform.SetParent(&parent->transform);
			}

		public:
			//aq::math::Vector3 GetFront() const;
		};


		

		class HierarcicalTransformSystem : public ecs::SystemBase
		{
		public:
			HierarcicalTransformSystem();
			~HierarcicalTransformSystem();

			void Update();


		private:
			static HierarcicalTransformSystem* instance_;


		public:
			static HierarcicalTransformSystem& Get() { return *instance_; }
			static bool IsAvailable() { return instance_ != nullptr; }
		};
	}
}
