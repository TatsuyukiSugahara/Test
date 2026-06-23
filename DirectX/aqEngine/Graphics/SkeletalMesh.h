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
				SkeletalModelLit, // SkeletalModelLit.fx (スキニング + ライティング)
			};

		private:
			std::shared_ptr<IVertexBuffer>   vertexBuffer_;
			std::shared_ptr<IIndexBuffer>    indexBuffer_;
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

			MaterialCBData materialCB_;
			ShaderType     shaderType_     = ShaderType::SkeletalModelLit;
			bool           castShadow_    = false;
			bool           receiveShadow_ = false;

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

			void SetSpecularIntensity(float v) { materialCB_.specularIntensity = v; }
			void SetGloss(float v)             { materialCB_.gloss = v; }
			void SetEmissiveScale(float v)     { materialCB_.emissiveScale = v; }
			void SetMaterialFlag(MaterialFlags flag, bool enable)
			{
				if (enable) materialCB_.flags |=  static_cast<uint32_t>(flag);
				else        materialCB_.flags &= ~static_cast<uint32_t>(flag);
			}

			const math::Vector4& GetParameter(uint32_t index) const { return materialCB_.params[index]; }
			void                 SetParameter(uint32_t index, const math::Vector4& v) { materialCB_.params[index] = v; }
			void ForEachParameter(const std::function<void(uint32_t, math::Vector4&)>& fn)
			{
				for (uint32_t i = 0; i < MATERIAL_PARAMETER_NUM; ++i)
					fn(i, materialCB_.params[i]);
			}

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
