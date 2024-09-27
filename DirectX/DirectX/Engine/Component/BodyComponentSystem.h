#pragma once
#include "../ECS/ECS.h"
#include "../Graphics/StaticMesh.h"
#include "../Graphics/RenderContext.h"


namespace engine
{
	namespace ecs
	{
		class StaticMeshComponent : public engine::ecs::IComponent
		{
			ecsComponent(engine::ecs::StaticMeshComponent);


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
			void Initialize();
			void Update();


		public:
			inline bool IsCompleted() const { return componentState_ == ComponentState::Completed; }


		public:
			inline engine::graphics::StaticMesh* GetStaticMesh() { return &staticMesh_; }
		};





		class RenderSystem : public engine::ecs::SystemBase
		{
		public:
			RenderSystem();
			~RenderSystem();
			void Update() override;
			void Render(engine::graphics::RenderContext& context);


		private:
			static RenderSystem* instance_;


		public:
			static RenderSystem& Get() { return *instance_; }
			static bool IsAvailable() { return instance_ != nullptr; }
		};



		/**
		 * ���̂��Փ˂�����ۂɎg�p����R���|�[�l���g
		 */
		class PhysicalBodyComponent : public engine::ecs::IComponent
		{

		};

		/**
		 * �Փ˔���Ɏg�p����R���|�[�l���g
		 */
		class GhostBodyComponent : public engine::ecs::IComponent
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