#pragma once
#include <string>
#include "ECS/ECS.h"
#include "Graphics/Camera.h"
#include "Graphics/StaticMesh.h"
#include "Graphics/SkeletalMesh.h"
#include "Graphics/RenderContext.h"
#include "Rendering/RenderFrame.h"
#include "Component/OceanComponent.h"


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

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				visitor.ReadOnly("state", IsCompleted() ? "Completed" : "Loading");
			}
#endif
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
			std::string metallicRoughnessPath_;
			aq::math::Matrix4x4 modelLocalMatrix_;
			bool textureLoadRequested_;
			aq::graphics::StaticMesh::ShaderType shaderType_;


		public:
			StaticMeshComponent();
			~StaticMeshComponent();
			void Update();
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix);
			/** ロード前（SetModelPath より前）に呼ぶこと。ロード後の変更は再初期化されない。 */
			void SetShaderType(aq::graphics::StaticMesh::ShaderType type) { shaderType_ = type; }
			/** PBR 専用。SetShaderType(PBRLit) の後、SetModelPath() の前に呼ぶこと。 */
			void SetMetallicRoughnessPath(const char* path) { metallicRoughnessPath_ = path ? path : ""; }


		public:
			inline bool IsCompleted()     const { return componentState_ == ComponentState::Completed; }
			inline const std::string& GetModelPath() const { return modelPath_; }

		public:
			inline aq::graphics::StaticMesh* GetStaticMesh() { return &staticMesh_; }

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				// コピー経由で編集して Enter で SetModelPath を呼ぶ（自己代入回避）
				std::string newModel   = modelPath_;
				std::string newTexture = texturePath_;
				const bool cm = visitor.FieldPath("Model Path",   newModel);
				const bool ct = visitor.FieldPath("Texture Path", newTexture);
				if (cm || ct)
					SetModelPath(newModel.c_str(),
					             newTexture.empty() ? nullptr : newTexture.c_str());
				visitor.ReadOnly("state", IsCompleted() ? "Completed" : "Loading");

				// PBR マテリアルパラメータ（PBR シェーダー時のみ表示）
				const bool isPBR =
					shaderType_ == aq::graphics::StaticMesh::ShaderType::PBRLit ||
					shaderType_ == aq::graphics::StaticMesh::ShaderType::TerrainPBRLit;
				if (isPBR)
				{
					visitor.Field("Metallic",      staticMesh_.MetallicRef());
					visitor.Field("Roughness",     staticMesh_.RoughnessRef());
					visitor.Field("Specular F0",   staticMesh_.SpecularRef());
					visitor.Field("EmissiveScale", staticMesh_.EmissiveScaleRef());
					visitor.Field("Translucent",   staticMesh_.TranslucentRef());
				}
			}
#endif
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
			std::string                      metallicRoughnessPath_;
			aq::math::Matrix4x4              modelLocalMatrix_;
			bool                             textureLoadRequested_;
			aq::graphics::SkeletalMesh::ShaderType shaderType_
				= aq::graphics::SkeletalMesh::ShaderType::SkeletalModelLit;

		public:
			SkeletalMeshComponent();
			~SkeletalMeshComponent();

			/** モデルパス (TKM v101) とオプションのテクスチャパスを設定する */
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix);
			/** ロード前（SetModelPath より前）に呼ぶこと。ロード後の変更は再初期化されない。 */
			void SetShaderType(aq::graphics::SkeletalMesh::ShaderType type) { shaderType_ = type; }
			/** PBR 専用。SetShaderType(SkeletalPBRLit) の後、SetModelPath() の前に呼ぶこと。 */
			void SetMetallicRoughnessPath(const char* path) { metallicRoughnessPath_ = path ? path : ""; }

			void Update();

			bool IsCompleted()            const { return componentState_ == ComponentState::Completed; }
			const std::string& GetModelPath() const { return modelPath_; }

			aq::graphics::SkeletalMesh* GetSkeletalMesh() { return &skeletalMesh_; }

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				std::string newModel   = modelPath_;
				std::string newTexture = texturePath_;
				const bool cm = visitor.FieldPath("Model Path",   newModel);
				const bool ct = visitor.FieldPath("Texture Path", newTexture);
				if (cm || ct)
					SetModelPath(newModel.c_str(),
					             newTexture.empty() ? nullptr : newTexture.c_str());
				visitor.ReadOnly("state", IsCompleted() ? "Completed" : "Loading");

				// PBR マテリアルパラメータ（PBR シェーダー時のみ表示）
				if (shaderType_ == aq::graphics::SkeletalMesh::ShaderType::SkeletalPBRLit)
				{
					visitor.Field("Metallic",      skeletalMesh_.MetallicRef());
					visitor.Field("Roughness",     skeletalMesh_.RoughnessRef());
					visitor.Field("Specular F0",   skeletalMesh_.SpecularRef());
					visitor.Field("EmissiveScale", skeletalMesh_.EmissiveScaleRef());
					visitor.Field("Translucent",   skeletalMesh_.TranslucentRef());
				}
			}
#endif
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
