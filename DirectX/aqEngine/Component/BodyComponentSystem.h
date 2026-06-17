#pragma once
#include <string>
#include "ECS/ECS.h"
#include "Graphics/Camera.h"
#include "Graphics/StaticMesh.h"
#include "Graphics/RenderContext.h"
#include "Rendering/RenderFrame.h"


namespace aq
{
	namespace ecs
	{
		class BoxStaticMeshComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::BoxStaticMeshComponent);

		private:
			enum class ComponentState : uint8_t
			{
				Loading,
				Completed,
			};

		private:
			ComponentState componentState_;

			aq::graphics::StaticMesh staticMesh_;


		public:
			BoxStaticMeshComponent();
			~BoxStaticMeshComponent();
			void Update();


		public:
			inline bool IsCompleted() const { return componentState_ == ComponentState::Completed; }


		public:
			inline aq::graphics::StaticMesh* GetStaticMesh() { return &staticMesh_; }
		};

		class StaticMeshComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::StaticMeshComponent);


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

			aq::res::RefMeshResource meshResouce_;
			aq::res::RefGPUResource  gpuResources_[static_cast<uint32_t>(aq::rendering::TextureSlot::Count)];
			aq::graphics::StaticMesh staticMesh_;
			std::string modelPath_;
			std::string texturePath_;
			aq::math::Matrix4x4 modelLocalMatrix_;
			bool textureLoadRequested_;
			aq::graphics::StaticMesh::ShaderType shaderType_;


		public:
			StaticMeshComponent();
			~StaticMeshComponent();
			void Update();
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix);
			void SetShaderType(aq::graphics::StaticMesh::ShaderType type) { shaderType_ = type; }


		public:
			inline bool IsCompleted() const { return componentState_ == ComponentState::Completed; }


		public:
			inline aq::graphics::StaticMesh* GetStaticMesh() { return &staticMesh_; }
		};





		class RenderSystem : public aq::ecs::SystemBase
		{
		public:
			RenderSystem();
			~RenderSystem();
			void Update() override;

			/** ECS を走査して RenderFrame を構築する。描画は行わない。 */
			void BuildRenderFrame(aq::rendering::RenderFrame& frame);
			void BuildRenderFrame(aq::rendering::RenderFrame& frame, aq::CameraType cameraType);

		private:
			static RenderSystem* instance_;


		public:
			static RenderSystem& Get() { return *instance_; }
			static bool IsAvailable() { return instance_ != nullptr; }
		};



		/**
		 * 物体を衝突させる際に使用するコンポーネント
		 */
		class PhysicalBodyComponent : public aq::ecs::IComponent
		{

		};

		/**
		 * 衝突判定に使用するコンポーネント
		 */
		class GhostBodyComponent : public aq::ecs::IComponent
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
