#pragma once
#include "../ECS/ECS.h"

namespace engine
{
	namespace ecs
	{
		struct Transform
		{
		public:
			engine::math::Vector3 position;
			engine::math::Vector3 localPosition;
			engine::math::Vector3 scale;
			engine::math::Vector3 localScale;
			engine::math::Vector3 angle;
			engine::math::Vector3 localAngle;
			engine::math::Quaternion rotation;
			engine::math::Quaternion localRotation;

			engine::math::Matrix4x4 rotationMatrix;
			engine::math::Matrix4x4 worldMatrix;

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
			ecsComponent(engine::ecs::TransformComponent);


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
			//engine::math::Vector3 GetFront() const;
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