#pragma once
#include "Component.h"
#include "../Graphics/StaticMesh.h"


namespace engine
{
	namespace component
	{
		class StaticMeshComponent : public engine::component::IComponent
		{
			engineComponent(engine::component::StaticMeshComponent);


		private:
			enum class ComponentState : uint8_t
			{
				Invalid,
				LoadRequest,
				Loading,
				Completed,
			};


		private:
			ComponentState componentState_;

			engine::res::RefMeshResource meshResouce_;
			engine::res::RefGPUResource gpuResource_;
			engine::graphics::StaticMesh staticMesh_;

			
		public:
			StaticMeshComponent(engine::IGameObject* gameObject);
			virtual ~StaticMeshComponent();

			void Start() override;
			void Update() override;
			void Render(graphics::RenderContext& context) override;
		};


		/**
		 * 物体を衝突させる際に使用するコンポーネント
		 */
		class PhysicalBodyComponent : public engine::component::IComponent
		{

		};

		/**
		 * 衝突判定に使用するコンポーネント
		 */
		class GhostBodyComponent : public engine::component::IComponent
		{

		};
		/**
		 * 球体形状の衝突判定に使用するコンポーネント
		 */
		//class SphereGhostBodyComponent
		//{
		//};
		/**
		 * メッシュ形状の衝突判定にしようするコンポーネント
		 */
		//class MeshGhostBodyComponent
		//{
		//};
	}
}