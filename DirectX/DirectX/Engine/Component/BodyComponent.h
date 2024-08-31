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
		 * ���̂��Փ˂�����ۂɎg�p����R���|�[�l���g
		 */
		class PhysicalBodyComponent : public engine::component::IComponent
		{

		};

		/**
		 * �Փ˔���Ɏg�p����R���|�[�l���g
		 */
		class GhostBodyComponent : public engine::component::IComponent
		{

		};
		/**
		 * ���̌`��̏Փ˔���Ɏg�p����R���|�[�l���g
		 */
		//class SphereGhostBodyComponent
		//{
		//};
		/**
		 * ���b�V���`��̏Փ˔���ɂ��悤����R���|�[�l���g
		 */
		//class MeshGhostBodyComponent
		//{
		//};
	}
}