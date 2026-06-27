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
				NormalModel,   // Model.fx    (ライトなし、後方互換)
				SimpleBox,     // SimpleBox.fx
				ModelLit,      // ModelLit.fx (フォワード、Blinn-Phong)
				TerrainLit,    // TerrainLit.fx (フォワード、スプラットマップ地形)
				OceanLit,      // OceanLit.fx (Gerstner 波・Fresnel 海面)
				PBRLit,        // PBRGBuffer.fx (ディファード PBR、static mesh)
				TerrainPBRLit, // TerrainPBRGBuffer.fx (ディファード PBR、terrain)
			};

		private:
			std::shared_ptr<IVertexBuffer>   vertexBuffer_;
			std::shared_ptr<IIndexBuffer>    indexBuffer_;
			std::shared_ptr<IIndexBuffer>    cullIndexBuffer_;  // クラスタ compact 描画用 動的IB
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

			MaterialCBData    materialCB_;
			PBRMaterialCBData pbrMaterialCB_;
			ShaderType        shaderType_    = ShaderType::NormalModel;
			bool              castShadow_    = false;
			bool              receiveShadow_ = false;

			aq::res::RefShaderResource gbufferPSShaderResource_;

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
			void                 SetParameter(uint32_t index, const math::Vector4& v)
			{
				materialCB_.params[index] = v;
				// PBR CB の _extra も同期（TerrainPBRGBuffer.fx が _extra[0].x をタイリングに使う）
				if (index < 7) pbrMaterialCB_._extra[index] = v;
			}
			void ForEachParameter(const std::function<void(uint32_t, math::Vector4&)>& fn)
			{
				for (uint32_t i = 0; i < MATERIAL_PARAMETER_NUM; ++i)
					fn(i, materialCB_.params[i]);
			}

			// PBR インスペクター用（#ifdef AQ_DEBUG_IMGUI 内で visitor.Field に渡す）
			float& MetallicRef()      { return pbrMaterialCB_.metallic; }
			float& RoughnessRef()     { return pbrMaterialCB_.roughness; }
			float& SpecularRef()      { return pbrMaterialCB_.specular; }
			float& EmissiveScaleRef() { return pbrMaterialCB_.emissiveScale; }
			float& TranslucentRef()   { return pbrMaterialCB_.dither; }

			void SetCastShadow(bool v)    { castShadow_ = v; }
			void SetReceiveShadow(bool v) { receiveShadow_ = v; SetMaterialFlag(MatFlag_ReceiveShadow, v); }
			// 投影デカールを受けるか（既定 true）。false で GBuffer に非対象マーカーを書く。
			void SetReceivesDecal(bool v) { SetMaterialFlag(MatFlag_NoDecal, !v); }
			void SetSamplerState(std::unique_ptr<ISamplerState> sampler) { samplerState_ = std::move(sampler); }

			void SetSpecularIntensity(float v) { materialCB_.specularIntensity = v; }
			void SetGloss(float v)             { materialCB_.gloss = v; }
			void SetEmissiveScale(float v)     { materialCB_.emissiveScale = v; pbrMaterialCB_.emissiveScale = v; }

			// PBR 専用 setter（SetShaderType(PBRLit) 後に呼ぶこと）
			void SetMetallic(float v)      { pbrMaterialCB_.metallic  = v; }
			void SetRoughness(float v)     { pbrMaterialCB_.roughness = v; }
			void SetSpecular(float v)      { pbrMaterialCB_.specular  = v; }
			// 擬似半透明（ディファード）。1.0=不透明, 0.0=透明。PBRLit シェーダー時のみ有効。
			void SetTranslucent(float v)   { pbrMaterialCB_.dither    = v; }
			void SetMetallicRoughnessTex(aq::res::RefGPUResource r)
			{
				SetTexture(rendering::TextureSlot::MetallicRoughness, r);
			}

			// 両 CB に同時反映（タイミング問題を回避）
			void SetMaterialFlag(MaterialFlags flag, bool enable)
			{
				uint32_t bit = static_cast<uint32_t>(flag);
				if (enable) { materialCB_.flags    |= bit; pbrMaterialCB_.flags |= bit; }
				else        { materialCB_.flags    &= ~bit; pbrMaterialCB_.flags &= ~bit; }
			}

			ShaderType GetShaderType() const { return shaderType_; }

			/** トライアングルカリング用クラスタ (MeshResource 経由のみ)。無ければ nullptr。 */
			const std::vector<graphics::MeshCluster>* GetClusters() const
			{
				return meshResource_ ? &meshResource_->GetClusters() : nullptr;
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
