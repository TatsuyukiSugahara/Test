#pragma once
#include <string>
#include "ECS/ECS.h"
#include "Graphics/Camera.h"
#include "Graphics/StaticMesh.h"
#include "Graphics/SkeletalMesh.h"
#include "Graphics/RenderContext.h"
#include "Rendering/RenderFrame.h"
#include "Component/OceanComponent.h"

namespace aq { namespace rendering { class IOcclusionTester; } }


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

			// 永続フィールドなし（プログラム生成のボックス）。
			// Prefab の構成要素として追加・生成できるよう空の Reflect を提供する。
			template <typename V>
			void Reflect(V&) {}
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

			// 永続フィールドの列挙（JSON 保存/読込）。常時コンパイル。
			// ImGui 編集は Inspect 側に分離している（パス入力のコミット制御が必要なため別実装）。
			// パスは raw メンバを直接列挙し、ロード副作用は OnDeserialized へ退避する
			// （SetModelPath を Reflect 内で呼ぶと shaderType 設定との順序が崩れるため）。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("model",   modelPath_,   "Model Path");
				visitor.FieldPath("texture", texturePath_, "Texture Path");
			}

			// deserialize 後に呼ぶ。読み込んだパスからメッシュのロードを発火する。
			void OnDeserialized()
			{
				if (!modelPath_.empty())
					SetModelPath(modelPath_.c_str(),
					             texturePath_.empty() ? nullptr : texturePath_.c_str());
			}
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

			// 永続フィールドの列挙（JSON 保存/読込）。常時コンパイル。StaticMesh と同方針。
			// パスは raw メンバを直接列挙し、ロード副作用は OnDeserialized へ退避する。
			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("model",   modelPath_,   "Model Path");
				visitor.FieldPath("texture", texturePath_, "Texture Path");
			}

			// deserialize 後に呼ぶ。読み込んだパスからメッシュのロードを発火する。
			void OnDeserialized()
			{
				if (!modelPath_.empty())
					SetModelPath(modelPath_.c_str(),
					             texturePath_.empty() ? nullptr : texturePath_.c_str());
			}
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

			// --- フラスタムカリング ---
			static void SetFrustumCullingEnabled(bool enabled) { frustumCullingEnabled_ = enabled; }
			static bool IsFrustumCullingEnabled()              { return frustumCullingEnabled_; }
			// 直近の BuildRenderFrame(Main) でのバウンディング持ちアイテム数と可視数
			static uint32_t GetCullingTotalCount()   { return cullingTotalCount_; }
			static uint32_t GetCullingVisibleCount() { return cullingVisibleCount_; }

			// --- オクリュージョンカリング (Hi-Z) ---
			static void SetOcclusionTester(const aq::rendering::IOcclusionTester* t) { occlusionTester_ = t; }
			static void SetOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled_ = enabled; }
			static bool IsOcclusionCullingEnabled()              { return occlusionCullingEnabled_; }
			static bool IsOcclusionAvailable()                   { return occlusionTester_ != nullptr; }
			// 直近の BuildRenderFrame(Main) でオクルードして除外した数
			static uint32_t GetOccludedCount() { return occludedCount_; }

			// --- トライアングル(クラスタ)カリング 統計 (描画はまだ削減しない・潜在効果の可視化) ---
			static void SetClusterStatsEnabled(bool e) { clusterStatsEnabled_ = e; }
			static bool IsClusterStatsEnabled()        { return clusterStatsEnabled_; }
			static uint32_t GetClusterTotal()      { return clusterTotal_; }
			static uint32_t GetClusterVisible()    { return clusterVisible_; }
			static uint32_t GetClusterTriTotal()   { return clusterTriTotal_; }
			static uint32_t GetClusterTriVisible() { return clusterTriVisible_; }
			static uint32_t GetClusterConeUsable() { return clusterConeUsable_; }
			static uint32_t GetClusterBackface()   { return clusterBackface_; }

		private:
			static RenderSystem* instance_;

			static bool     frustumCullingEnabled_;
			static uint32_t cullingTotalCount_;
			static uint32_t cullingVisibleCount_;

			static const aq::rendering::IOcclusionTester* occlusionTester_;
			static bool     occlusionCullingEnabled_;
			static uint32_t occludedCount_;

			static bool     clusterStatsEnabled_;
			static uint32_t clusterTotal_;
			static uint32_t clusterVisible_;
			static uint32_t clusterTriTotal_;
			static uint32_t clusterTriVisible_;
			static uint32_t clusterConeUsable_;
			static uint32_t clusterBackface_;


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
