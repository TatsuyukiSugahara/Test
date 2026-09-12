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
namespace aq { namespace graphics { class InstancedStaticMesh; } }


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
			bool           visible_ = true;   // false で描画から外す(退避・遅延破棄などに使う)

			aq::graphics::StaticMesh staticMesh_;

			/** 箱の単色 (SimpleBox.fx の params[0])。既定は赤 */
			aq::math::Vector4 color_ = aq::math::Vector4(1.0f, 0.0f, 0.0f, 1.0f);


		public:
			BoxStaticMeshComponent();
			~BoxStaticMeshComponent();
			void Update();

			/** 箱の単色を設定する。初期化前に呼んだ場合は初期化完了時にまとめて反映される */
			void SetColor(const aq::math::Vector4& color);


		public:
			inline bool IsCompleted() const { return componentState_ == ComponentState::Completed; }

			inline const aq::math::Vector4& GetColor() const { return color_; }

			// 描画対象から外す/戻す。フラスタム外の箱はカリングされるが、視界内でも
			// 確実に描画をスキップさせたい場合(退避・遅延破棄など)の手段として使う。
			inline void SetVisible(bool v) { visible_ = v; }
			inline bool IsVisible() const  { return visible_; }

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


		public:
			/**
			 * 箱ジオメトリをインスタンス描画用の共有メッシュとして名前登録する。
			 * 単位キューブのローカル AABB まで設定するので per-instance フラスタムカリングが効く。
			 * 既に同名が登録済みならそれを返す。デバイス準備後・ForEach 外の安全点で呼ぶこと。
			 * @param name InstancedStaticMeshComponent::SetMesh に渡す登録名
			 * @return 登録された (or 既存の) 共有メッシュ。寿命は名前レジストリが持つ
			 */
			static aq::graphics::InstancedStaticMesh* RegisterInstancedMesh(const char* name);
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
			aq::math::Vector3   modelRotationEuler_ = aq::math::Vector3(0.0f, 0.0f, 0.0f); // モデル向き補正(度,XYZ)
			bool textureLoadRequested_;
			aq::graphics::StaticMesh::ShaderType shaderType_;


		public:
			StaticMeshComponent();
			~StaticMeshComponent();
			void Update();
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix);
			/** モデル向き補正(度,XYZ)から回転行列を作りモデルローカル行列へ反映。
			 *  インスペクター/JSON から設定する取込み向き補正。エンティティ回転とは別。 */
			void ApplyModelRotation()
			{
				const float d2r = 3.14159265358979f / 180.0f;
				aq::math::Matrix4x4 rx, ry, rz, xy, xyz;
				rx.MakeRotationX(modelRotationEuler_.x * d2r);
				ry.MakeRotationY(modelRotationEuler_.y * d2r);
				rz.MakeRotationZ(modelRotationEuler_.z * d2r);
				xy.Mull(rx, ry);
				xyz.Mull(xy, rz);
				SetModelLocalMatrix(xyz);
			}
			/** モデル向き補正(度,XYZ)を設定して即反映する (コード/デモ用)。 */
			void SetModelRotationEuler(const aq::math::Vector3& deg) { modelRotationEuler_ = deg; ApplyModelRotation(); }
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
				// モデル向き補正 (度, XYZ)。エンティティ回転とは別の「取込み向き」。編集で即反映。
				visitor.Field("Model Rotation (deg)", modelRotationEuler_);
				ApplyModelRotation();

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
				visitor.Field("modelRotation", modelRotationEuler_);
			}

			// deserialize 後に呼ぶ。読み込んだパスからメッシュのロードを発火する。
			void OnDeserialized()
			{
				if (!modelPath_.empty())
					SetModelPath(modelPath_.c_str(),
					             texturePath_.empty() ? nullptr : texturePath_.c_str());
				ApplyModelRotation();
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
			aq::math::Vector3               modelRotationEuler_ = aq::math::Vector3(0.0f, 0.0f, 0.0f); // モデル向き補正(度,XYZ)
			bool                             textureLoadRequested_;
			aq::graphics::SkeletalMesh::ShaderType shaderType_
				= aq::graphics::SkeletalMesh::ShaderType::SkeletalModelLit;
			bool                             visible_ = true;   // false の間 gather から外す (描画しない)

		public:
			SkeletalMeshComponent();
			~SkeletalMeshComponent();

			/**
			 * 描画するか。false の間は gather がこのメッシュを積まないので、
			 * 生成済みのエンティティを消さずに表示だけ止められる
			 * (スケールを 0 にする等の細工をしなくてよい)。
			 */
			void SetVisible(const bool visible) { visible_ = visible; }
			bool IsVisible() const              { return visible_; }

			/** モデルパス (TKM v101) とオプションのテクスチャパスを設定する */
			void SetModelPath(const char* modelPath, const char* texturePath = nullptr);
			void SetModelLocalMatrix(const aq::math::Matrix4x4& localMatrix);
			/** モデル向き補正(度,XYZ)から回転行列を作りモデルローカル行列へ反映。
			 *  インスペクター/JSON から設定する取込み向き補正。エンティティ回転とは別。 */
			void ApplyModelRotation()
			{
				const float d2r = 3.14159265358979f / 180.0f;
				aq::math::Matrix4x4 rx, ry, rz, xy, xyz;
				rx.MakeRotationX(modelRotationEuler_.x * d2r);
				ry.MakeRotationY(modelRotationEuler_.y * d2r);
				rz.MakeRotationZ(modelRotationEuler_.z * d2r);
				xy.Mull(rx, ry);
				xyz.Mull(xy, rz);
				SetModelLocalMatrix(xyz);
			}
			/** モデル向き補正(度,XYZ)を設定して即反映する (コード/デモ用)。 */
			void SetModelRotationEuler(const aq::math::Vector3& deg) { modelRotationEuler_ = deg; ApplyModelRotation(); }
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
				// モデル向き補正 (度, XYZ)。エンティティ回転とは別の「取込み向き」。編集で即反映。
				visitor.Field("Model Rotation (deg)", modelRotationEuler_);
				ApplyModelRotation();

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
				visitor.Field("modelRotation", modelRotationEuler_);
			}

			// deserialize 後に呼ぶ。読み込んだパスからメッシュのロードを発火する。
			void OnDeserialized()
			{
				if (!modelPath_.empty())
					SetModelPath(modelPath_.c_str(),
					             texturePath_.empty() ? nullptr : texturePath_.c_str());
				ApplyModelRotation();
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

			/**
			 * カメラ直指定版 (分割画面のビュー毎構築用)。
			 * @param enableFrustumCulling ビュー視錐台でのフラスタムカリングを行うか
			 * @param enableOcclusion      Hi-Z オクリュージョンを行うか (複数ビューでは単一カメラ前提が崩れるため false 推奨)
			 * @param updateStats          カリング統計 (デバッグ表示) を更新するか (1 ビューのみ true にする)
			 * @param gatherInstances      インスタンス描画の gather + Flush を行うか
			 *                             (Flush は 1 フレーム 1 回。分割画面では先頭ビューのみ true にし、
			 *                              以降のビューはビュー0 の視錐台で切った結果を共有する。
			 *                              true でも同一フレーム 2 回目以降は 1 回目の結果を再利用する)
			 */
			void BuildRenderFrame(aq::rendering::RenderFrame& frame, const aq::Camera& viewCamera,
			                      const bool enableFrustumCulling, const bool enableOcclusion,
			                      const bool updateStats, const bool gatherInstances = true);

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

			/** 同一フレーム内で gather + Flush を済ませたか (Update の先頭でリセットする) */
			static bool     instanceGatherDone_;

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
