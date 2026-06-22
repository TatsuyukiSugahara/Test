#pragma once
#include <memory>
#include <functional>
#include "Resource/Resource.h"
#include "Math/Matrix.h"
#include "IBuffer.h"
#include "IShader.h"
#include "ISamplerState.h"
#include "RenderContext.h"
#include "Lighting.h"
#include "Rendering/RenderFrame.h"


namespace aq
{
	namespace graphics
	{
		class StaticMesh
		{
		public:
			enum class ShaderType
			{
				NormalModel,  // Model.fx    (ライトなし、後方互換)
				SimpleBox,    // SimpleBox.fx
				ModelLit,     // ModelLit.fx (フルライティング)
				TerrainLit,   // TerrainLit.fx (スプラットマップ地形)
				OceanLit,     // OceanLit.fx (Gerstner 波・Fresnel 海面)
			};

		private:
			std::shared_ptr<IVertexBuffer>   vertexBuffer_;
			std::shared_ptr<IIndexBuffer>    indexBuffer_;
			uint32_t                         indicesSize_;
			std::shared_ptr<ISamplerState>   samplerState_;
			math::Matrix4x4                  worldMatrix_;
			math::Matrix4x4                  localMatrix_;
			bool                             isInitialized_;

			aq::res::RefMeshResource  meshResource_;
			aq::res::RefShaderResource vsShaderResource_;
			aq::res::RefShaderResource psShaderResource_;

			// テクスチャリソース t0-t3
			aq::res::RefGPUResource gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Count)];
			std::shared_ptr<IShaderResourceView> textureOverrides_[static_cast<uint32_t>(rendering::TextureSlot::Count)];

			MaterialCBData materialCB_;
			ShaderType     shaderType_    = ShaderType::NormalModel;
			bool           castShadow_    = false;
			bool           receiveShadow_ = false;

		public:
			StaticMesh();
			~StaticMesh();

			void Initialize(aq::res::RefMeshResource meshResource,
			                aq::res::RefGPUResource  albedoResource,
			                const ShaderType             shaderType);
			void Initialize(const void* vertexBuffer, const uint32_t vertexNum,
			                const void* indexBuffer,  const uint32_t indexNum,
			                const ShaderType shaderType);
			/** 動的VBで初期化 (HeightmapChunk ペイント用) */
			void InitializeDynamic(const void* vertexBuffer, const uint32_t vertexNum,
			                       const void* indexBuffer,  const uint32_t indexNum,
			                       const ShaderType shaderType);
			/** 動的VBの頂点データを更新する (InitializeDynamic 後のみ有効) */
			void UpdateVertices(const VertexData* verts, uint32_t count);
			void SetLocalMatrix(const math::Matrix4x4& localMatrix);
			void Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale);

			/** テクスチャを個別スロットに設定する (Initialize 後に呼ぶ) */
			void SetTexture(rendering::TextureSlot slot, aq::res::RefGPUResource resource);
			void SetTextureOverride(rendering::TextureSlot slot, std::shared_ptr<IShaderResourceView> srv);
			void ClearTextureOverride(rendering::TextureSlot slot);

			const math::Vector4& GetParameter(uint32_t index) const { return materialCB_.params[index]; }
			void                 SetParameter(uint32_t index, const math::Vector4& v) { materialCB_.params[index] = v; }
			void ForEachParameter(const std::function<void(uint32_t, math::Vector4&)>& fn)
			{
				for (uint32_t i = 0; i < MATERIAL_PARAMETER_NUM; ++i)
					fn(i, materialCB_.params[i]);
			}

			void SetCastShadow(bool v)    { castShadow_ = v; }
			void SetReceiveShadow(bool v) { receiveShadow_ = v; SetMaterialFlag(MatFlag_ReceiveShadow, v); }
			void SetSamplerState(std::unique_ptr<ISamplerState> sampler) { samplerState_ = std::move(sampler); }

			void SetSpecularIntensity(float v) { materialCB_.specularIntensity = v; }
			void SetGloss(float v)             { materialCB_.gloss = v; }
			void SetEmissiveScale(float v)     { materialCB_.emissiveScale = v; }
			void SetMaterialFlag(MaterialFlags flag, bool enable)
			{
				if (enable) materialCB_.flags |=  static_cast<uint32_t>(flag);
				else        materialCB_.flags &= ~static_cast<uint32_t>(flag);
			}

			/**
			 * 描画に必要な情報を RenderItem へコピーする。
			 * ロードが完了していない場合は false を返す。
			 */
			bool FillRenderItem(rendering::RenderItem& item) const;

		private:
			void Initialize(const ShaderType shaderType);
		};
	}
}
