#pragma once
#include "Component.h"


namespace engine
{
	namespace component
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


		class TransformComponent : public IComponent
		{
			engineComponent(engine::component::TransformComponent);


		private:
			Transform transform_;


		public:
			TransformComponent(engine::IGameObject* gameObject);
			~TransformComponent();
			void Start() override;
			void Update() override;
			void Render(graphics::RenderContext& context) override {};


		public:


		public:
			void SetParent(TransformComponent* parent)
			{
				transform_.SetParent(&parent->transform_);
			}

		public:
			//engine::math::Vector3 GetFront() const;
		};
	}
}