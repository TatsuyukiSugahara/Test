#pragma once
#include <string>
#include "../ECS/ECS.h"
#include "../Graphics/StaticMesh.h"
#include "../Graphics/RenderContext.h"
#include "../Rendering/RenderFrame.h"


namespace engine
{
	namespace ecs
	{
		class BoxStaticMeshComponent : public engine::ecs::IComponent
		{
			ecsComponent(engine::ecs::BoxStaticMeshComponent);

		private:
			enum class ComponentState : uint8_t
			{
				Loading,
				Completed,
			};

		private:
			ComponentState componentState_;

			engine::graphics::StaticMesh staticMesh_;


		public:
			BoxStaticMeshComponent();
			~BoxStaticMeshComponent();
			void Update();


		public:
			inline bool IsCompleted() const { return componentState_ == ComponentState::Completed; }


		public:
			inline engine::graphics::StaticMesh* GetStaticMesh() { return &staticMesh_; }
		};

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
			std::string modelPath_;
			std::string texturePath_;
			engine::math::Matrix4x4 modelLocalMatrix_;
			bool textureLoadRequested_;

			
		public:
			StaticMeshComponent();
			~StaticMeshComponent();
			void Update();
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const engine::math::Matrix4x4& localMatrix);


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

			/** ECS を走査して RenderFrame を構築する。描画は行わない。 */
			void Gather(engine::rendering::RenderFrame& frame);


		private:
			static RenderSystem* instance_;


		public:
			static RenderSystem& Get() { return *instance_; }
			static bool IsAvailable() { return instance_ != nullptr; }
		};



		/**
		 * 物体を衝突させる際に使用するコンポーネント
		 */
		class PhysicalBodyComponent : public engine::ecs::IComponent
		{

		};

		/**
		 * 衝突判定に使用するコンポーネント
		 */
		class GhostBodyComponent : public engine::ecs::IComponent
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
