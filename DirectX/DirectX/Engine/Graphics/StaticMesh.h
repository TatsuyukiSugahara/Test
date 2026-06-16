#pragma once
#include <memory>
#include "Resource/Resource.h"
#include "Math/Matrix.h"
#include "IBuffer.h"
#include "IShader.h"
#include "ISamplerState.h"
#include "RenderContext.h"
#include "Lighting.h"
#include "Rendering/RenderFrame.h"


namespace engine
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
			};

		private:
			std::shared_ptr<IVertexBuffer>   vertexBuffer_;
			std::shared_ptr<IIndexBuffer>    indexBuffer_;
			uint32_t                         indicesSize_;
			std::shared_ptr<ISamplerState>   samplerState_;
			math::Matrix4x4                  worldMatrix_;
			math::Matrix4x4                  localMatrix_;
			bool                             isInitialized_;

			engine::res::RefMeshResource  meshResource_;
			engine::res::RefShaderResource vsShaderResource_;
			engine::res::RefShaderResource psShaderResource_;

			// テクスチャリソース t0-t3
			engine::res::RefGPUResource gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Count)];

			MaterialCBData materialCB_;
			bool           castShadow_    = false;
			bool           receiveShadow_ = false;

		public:
			StaticMesh();
			~StaticMesh();

			void Initialize(engine::res::RefMeshResource meshResource,
			                engine::res::RefGPUResource  albedoResource,
			                const ShaderType             shaderType);
			void Initialize(const void* vertexBuffer, const uint32_t vertexNum,
			                const void* indexBuffer,  const uint32_t indexNum,
			                const ShaderType shaderType);
			void SetLocalMatrix(const math::Matrix4x4& localMatrix);
			void Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale);

			/** テクスチャを個別スロットに設定する (Initialize 後に呼ぶ) */
			void SetTexture(rendering::TextureSlot slot, engine::res::RefGPUResource resource);

			/** ユーザー自由パラメータ (HLSL: params[index]) */
			math::Vector4&       Param(uint32_t index)       { return materialCB_.params[index]; }
			const math::Vector4& Param(uint32_t index) const { return materialCB_.params[index]; }

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
