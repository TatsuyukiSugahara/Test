#pragma once
#include <memory>
#include <vector>
#include <functional>
#include "Resource/Resource.h"
#include "Math/Matrix.h"
#include "IBuffer.h"
#include "IShader.h"
#include "ISamplerState.h"
#include "Lighting.h"
#include "Rendering/RenderFrame.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * スケルタルメッシュ (ボーンによるスキニングあり)
		 *
		 * StaticMesh との違い:
		 *  - SkinnedVertexData (ボーンウェイト/インデックス付き頂点) を使用する
		 *  - AnimationComponent が CalcBoneMatrices() でボーン行列を計算し SetBoneMatrices() で渡す
		 *  - FillRenderItem() でボーン行列を RenderItem に載せる (シェーダー側でスキニング処理)
		 *  - ボーンのないファイル (TKM v100) は StaticMesh で読み込む
		 *  - ボーンのあるファイル (TKM v101) のみ対応
		 */
		class SkeletalMesh
		{
		public:
			enum class ShaderType
			{
				SkeletalModelLit, // SkeletalModelLit.fx (フォワード、スキニング + Blinn-Phong)
				SkeletalPBRLit,   // SkeletalPBRGBuffer.fx (ディファード PBR、スキニング)
			};

		private:
			std::shared_ptr<IVertexBuffer>   vertexBuffer_;
			std::shared_ptr<IIndexBuffer>    indexBuffer_;
			std::shared_ptr<IIndexBuffer>    cullIndexBuffer_;  // クラスタ compact 描画用 動的IB (CPU方式)
			GpuClusterBuffers                gpuClusterBuffers_; // GPU 駆動クラスタカリング用
			uint32_t                         indicesSize_;
			std::shared_ptr<ISamplerState>   samplerState_;
			math::Matrix4x4                  worldMatrix_;
			math::Matrix4x4                  localMatrix_;
			bool                             isInitialized_;

			res::RefSkeletalMeshResource skeletalMeshResource_;
			res::RefShaderResource       vsShaderResource_;
			res::RefShaderResource       psShaderResource_;
			res::RefShaderResource       gbufferPSShaderResource_;

			res::RefGPUResource gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Count)];

			MaterialCBData    materialCB_;
			PBRMaterialCBData pbrMaterialCB_;
			ShaderType        shaderType_    = ShaderType::SkeletalModelLit;
			bool              castShadow_    = false;
			bool              receiveShadow_ = false;

			// AnimationComponent から毎フレーム更新される。nullptr = バインドポーズ (単位行列)
			std::shared_ptr<std::vector<math::Matrix4x4>> boneMatrices_;

		public:
			SkeletalMesh();
			~SkeletalMesh();

			void Initialize(res::RefSkeletalMeshResource meshResource,
			                res::RefGPUResource          albedoResource,
			                ShaderType shaderType = ShaderType::SkeletalModelLit);

			void SetLocalMatrix(const math::Matrix4x4& localMatrix);
			void Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale);

			void SetTexture(rendering::TextureSlot slot, res::RefGPUResource resource);

			void SetCastShadow(bool v)    { castShadow_ = v; }
			void SetReceiveShadow(bool v) { receiveShadow_ = v; SetMaterialFlag(MatFlag_ReceiveShadow, v); }
			// 投影デカールを受けるか（既定 true）。false で GBuffer に非対象マーカーを書く。
			void SetReceivesDecal(bool v) { SetMaterialFlag(MatFlag_NoDecal, !v); }

			void SetSpecularIntensity(float v) { materialCB_.specularIntensity = v; }
			void SetGloss(float v)             { materialCB_.gloss = v; }
			void SetEmissiveScale(float v)     { materialCB_.emissiveScale = v; pbrMaterialCB_.emissiveScale = v; }

			// PBR 専用 setter
			void SetMetallic(float v)  { pbrMaterialCB_.metallic  = v; }
			void SetRoughness(float v) { pbrMaterialCB_.roughness = v; }
			void SetSpecular(float v)  { pbrMaterialCB_.specular  = v; }
			// 擬似半透明（ディファード）。1.0=不透明, 0.0=透明。SkeletalPBRLit シェーダー時のみ有効。
			void SetTranslucent(float v) { pbrMaterialCB_.dither   = v; }
			void SetMetallicRoughnessTex(res::RefGPUResource r)
			{
				SetTexture(rendering::TextureSlot::MetallicRoughness, r);
			}

			// 両 CB に同時反映（SetReceiveShadow 等がロード前に呼ばれても正しく動く）
			void SetMaterialFlag(MaterialFlags flag, bool enable)
			{
				uint32_t bit = static_cast<uint32_t>(flag);
				if (enable) { materialCB_.flags |= bit;  pbrMaterialCB_.flags |= bit;  }
				else        { materialCB_.flags &= ~bit; pbrMaterialCB_.flags &= ~bit; }
			}

			ShaderType GetShaderType() const { return shaderType_; }

			/** トライアングルカリング用クラスタ (バインドポーズ基準)。無ければ nullptr。 */
			const std::vector<graphics::MeshCluster>* GetClusters() const
			{
				return skeletalMeshResource_ ? &skeletalMeshResource_->GetClusters() : nullptr;
			}

			const math::Vector4& GetParameter(uint32_t index) const { return materialCB_.params[index]; }
			void                 SetParameter(uint32_t index, const math::Vector4& v)
			{
				materialCB_.params[index] = v;
				if (index < 7) pbrMaterialCB_._extra[index] = v;
			}
			void ForEachParameter(const std::function<void(uint32_t, math::Vector4&)>& fn)
			{
				for (uint32_t i = 0; i < MATERIAL_PARAMETER_NUM; ++i)
					fn(i, materialCB_.params[i]);
			}

			// PBR インスペクター用
			float& MetallicRef()      { return pbrMaterialCB_.metallic; }
			float& RoughnessRef()     { return pbrMaterialCB_.roughness; }
			float& SpecularRef()      { return pbrMaterialCB_.specular; }
			float& EmissiveScaleRef() { return pbrMaterialCB_.emissiveScale; }
			float& TranslucentRef()   { return pbrMaterialCB_.dither; }

			/** AnimationComponent からボーン行列を注入する */
			void SetBoneMatrices(std::shared_ptr<std::vector<math::Matrix4x4>> matrices)
			{
				if (matrices && !matrices->empty()) {
					boneMatrices_ = matrices;
				}
			}

			/**
			 * 描画に必要な情報を RenderItem へコピーする。
			 * ロードが完了していない場合は false を返す。
			 */
			bool FillRenderItem(rendering::RenderItem& item) const;

			bool     IsInitialized() const { return isInitialized_; }
			uint32_t GetBoneCount()  const;

			/** SkeletalMeshResource が持つボーン配列を返す (アニメーション計算に使用) */
			const std::vector<res::BoneData>* GetBones() const;

		private:
			void Initialize(ShaderType shaderType);
		};
	}
}
