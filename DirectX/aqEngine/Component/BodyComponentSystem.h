#pragma once
#include <string>
#include "ECS/ECS.h"
#include "Graphics/Camera.h"
#include "Graphics/StaticMesh.h"
#include "Graphics/SkeletalMesh.h"
#include "Graphics/AnimationClip.h"
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




		/**
		 * スケルタルメッシュコンポーネント
		 *
		 * UE の SkeletalMeshComponent に相当。ボーンのある TKM v101 ファイルを読み込む。
		 * ボーンのない TKM v100 ファイルは StaticMeshComponent を使用すること。
		 * AnimationComponent と組み合わせることでアニメーションを再生できる。
		 */
		class SkeletalMeshComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::SkeletalMeshComponent);

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

			aq::res::RefSkeletalMeshResource skeletalMeshResource_;
			aq::res::RefGPUResource          gpuResources_[static_cast<uint32_t>(aq::rendering::TextureSlot::Count)];
			aq::graphics::SkeletalMesh       skeletalMesh_;
			std::string                      modelPath_;
			std::string                      texturePath_;
			aq::math::Matrix4x4              modelLocalMatrix_;
			bool                             textureLoadRequested_;

		public:
			SkeletalMeshComponent();
			~SkeletalMeshComponent();

			/** モデルパス (TKM v101) とオプションのテクスチャパスを設定する */
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix);

			void Update();

			bool IsCompleted() const { return componentState_ == ComponentState::Completed; }

			aq::graphics::SkeletalMesh* GetSkeletalMesh() { return &skeletalMesh_; }
		};



		
		/**
		 * アニメーションコンポーネント
		 *
		 * UE の AnimInstance に相当。TKA ファイルからアニメーションを読み込み、
		 * SkeletalMeshComponent のボーン行列を毎フレーム更新する。
		 */
		class AnimationComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::AnimationComponent);

		private:
			aq::graphics::AnimationClip animationClip_;
			aq::res::RefAnimationResource animationResource_;
			std::string  animationPath_;
			float        currentTime_;
			float        playSpeed_;
			bool         isPlaying_;
			bool         isLooping_;
			bool         loadRequested_;

		public:
			AnimationComponent();
			~AnimationComponent() = default;

			/** アニメーションファイルパス (TKA) を設定する */
			void SetAnimationPath(const char* path);

			/** アニメーションを再生する */
			void Play(bool looping = true);
			void Stop() { isPlaying_ = false; }

			float GetCurrentTime() const { return currentTime_; }
			float GetPlaySpeed()   const { return playSpeed_;   }
			void  SetPlaySpeed(float speed) { playSpeed_ = speed; }

			/**
			 * アニメーション更新。SkeletalMeshComponent と連携してボーン行列を計算する。
			 * RenderSystem::Update() から呼ばれる。
			 */
			void Update(float deltaTime, SkeletalMeshComponent* skelMeshComp);
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
