#include "aq.h"
#include "StaticMesh.h"
#include "MeshRenderHelper.h"


namespace aq
{
	namespace graphics
	{
		namespace
		{
			struct ShaderInformation
			{
				const char* vsFilePath;
				const char* vsFuncName;
				const char* psFilePath;
				const char* psFuncName;
				// G-Buffer パス専用 PS。nullptr = forward-only（ディファード不要）
				const char* gbufferPSFilePath;
				const char* gbufferPSFuncName;
			};
			ShaderInformation shaderInformations[] = {
				{ "Assets/Shader/Model.fx",      "VSMain", "Assets/Shader/Model.fx",      "PSMain", nullptr,                                 nullptr  },  // NormalModel
				{ "Assets/Shader/SimpleBox.fx",  "VSMain", "Assets/Shader/SimpleBox.fx",  "PSMain", nullptr,                                 nullptr  },  // SimpleBox
				{ "Assets/Shader/ModelLit.fx",   "VSMain", "Assets/Shader/ModelLit.fx",   "PSMain", nullptr,                                 nullptr  },  // ModelLit (forward-only; GBuffer は PBRLit へ移行)
				{ "Assets/Shader/TerrainLit.fx", "VSMain", "Assets/Shader/TerrainLit.fx", "PSMain", nullptr,                                 nullptr  },  // TerrainLit (forward-only)
				{ "Assets/Shader/OceanLit.fx",   "VSMain", "Assets/Shader/OceanLit.fx",   "PSMain", nullptr,                                 nullptr  },  // OceanLit
				{ "Assets/Shader/ModelLit.fx",   "VSMain", "Assets/Shader/ModelLit.fx",   "PSMain", "Assets/Shader/PBRGBuffer.fx",           "PSMain" },  // PBRLit
				{ "Assets/Shader/TerrainLit.fx", "VSMain", "Assets/Shader/TerrainLit.fx", "PSMain", "Assets/Shader/TerrainPBRGBuffer.fx",    "PSMain" },  // TerrainPBRLit
			};
		}


		StaticMesh::StaticMesh()
			: indicesSize_(0)
			, worldMatrix_(math::Matrix4x4::Identity)
			, localMatrix_(math::Matrix4x4::Identity)
			, isInitialized_(false)
		{
		}


		StaticMesh::~StaticMesh()
		{
		}


		void StaticMesh::Initialize(aq::res::RefMeshResource meshResource,
		                            aq::res::RefGPUResource  albedoResource,
		                            const ShaderType             shaderType)
		{
			meshResource_ = meshResource;
			gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Albedo)] = albedoResource;
			isInitialized_ = false;

			if (!meshResource_ || meshResource_->GetVerticsSize() == 0 || meshResource_->GetIndicesSize() == 0) {
				return;
			}

			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(
				meshResource_->GetVerticsSize(),
				sizeof(aq::graphics::VertexData),
				meshResource_->GetVertics()->data()
			);
			indexBuffer_ = GraphicsDevice::Get().CreateIndexBuffer(
				meshResource_->GetIndicesSize(),
				meshResource_->GetIndices()->data()
			);
			indicesSize_ = meshResource_->GetIndicesSize();

			// クラスタカリング用: 並べ替えインデックスで動的IBを作る (CPU 方式の compact 宛先)
			const std::vector<uint32_t>& reordered = meshResource_->GetReorderedIndices();
			if (!reordered.empty())
			{
				cullIndexBuffer_ = GraphicsDevice::Get().CreateDynamicIndexBuffer(
					static_cast<uint32_t>(reordered.size()), IndexFormat::UInt32, reordered.data());
			}
			// GPU 駆動クラスタカリング用バッファ
			gpuClusterBuffers_.Create(meshResource_->GetClusters(), reordered);

			Initialize(shaderType);
		}


		void StaticMesh::Initialize(const void* vertexBuffer, const uint32_t vertexNum,
		                            const void* indexBuffer,  const uint32_t indexNum,
		                            const ShaderType shaderType)
		{
			isInitialized_ = false;
			if (!vertexBuffer || vertexNum == 0 || !indexBuffer || indexNum == 0) {
				return;
			}

			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(vertexNum, sizeof(aq::graphics::VertexData), vertexBuffer);
			indexBuffer_  = GraphicsDevice::Get().CreateIndexBuffer(indexNum, indexBuffer);
			indicesSize_  = indexNum;

			Initialize(shaderType);
		}


		void StaticMesh::InitializeDynamic(const void* vb, const uint32_t vCount,
		                                   const void* ib, const uint32_t iCount,
		                                   const ShaderType shaderType)
		{
			isInitialized_ = false;
			if (!vb || vCount == 0 || !ib || iCount == 0) return;
			vertexBuffer_ = GraphicsDevice::Get().CreateDynamicVertexBuffer(vCount, sizeof(VertexData), vb);
			indexBuffer_  = GraphicsDevice::Get().CreateIndexBuffer(iCount, ib);
			indicesSize_  = iCount;
			Initialize(shaderType);
		}

		void StaticMesh::UpdateVertices(const VertexData* verts, uint32_t count)
		{
			if (!vertexBuffer_) return;
			vertexBuffer_->Update(verts, count * sizeof(VertexData));
		}

		void StaticMesh::Initialize(const ShaderType shaderType)
		{
			shaderType_ = shaderType;
			const ShaderInformation& info = shaderInformations[static_cast<uint8_t>(shaderType)];
			vsShaderResource_ = aq::res::ResourceManager::Get().LoadShader(info.vsFilePath, info.vsFuncName, IShader::ShaderType::VS);
			psShaderResource_ = aq::res::ResourceManager::Get().LoadShader(info.psFilePath, info.psFuncName, IShader::ShaderType::PS);

			// G-Buffer PS はディファード対象シェーダーのみロード（パスが nullptr なら forward-only）
			if (info.gbufferPSFilePath != nullptr)
			{
				gbufferPSShaderResource_ = aq::res::ResourceManager::Get().LoadShader(
					info.gbufferPSFilePath, info.gbufferPSFuncName, IShader::ShaderType::PS);
			}
			else
			{
				gbufferPSShaderResource_.reset();
			}

			const bool hasAlbedo = gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Albedo)] != nullptr;
			if (hasAlbedo)
			{
				SamplerDesc samplerDesc;
				samplerDesc.filter   = FilterMode::MinMagMipLinear;
				samplerDesc.addressU = AddressMode::Clamp;
				samplerDesc.addressV = AddressMode::Clamp;
				samplerDesc.addressW = AddressMode::Clamp;
				samplerState_ = GraphicsDevice::Get().CreateSamplerState(samplerDesc);
			}

			isInitialized_ = vertexBuffer_ && indexBuffer_;
		}


		void StaticMesh::SetLocalMatrix(const math::Matrix4x4& localMatrix)
		{
			localMatrix_ = localMatrix;
		}


		void StaticMesh::Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale)
		{
			math::Matrix4x4 scaleMatrix, rotationMatrix, translationMatrix, localScaleMatrix, localRotationMatrix;
			scaleMatrix.MakeScaling(scale);
			rotationMatrix.MakeRotationFromQuaternion(rotation);
			translationMatrix.MakeTranslation(translation);
			localScaleMatrix.Mull(localMatrix_, scaleMatrix);
			localRotationMatrix.Mull(localScaleMatrix, rotationMatrix);
			worldMatrix_.Mull(localRotationMatrix, translationMatrix);
		}


		void StaticMesh::SetTexture(rendering::TextureSlot slot, aq::res::RefGPUResource resource)
		{
			const uint32_t idx = static_cast<uint32_t>(slot);
			gpuResources_[idx] = resource;
			textureOverrides_[idx].reset();

			// Normal/Specular/Emissive フラグを自動更新
			MaterialFlags flag = MatFlag_HasNormal;
			switch (slot)
			{
			case rendering::TextureSlot::Normal:   flag = MatFlag_HasNormal;   break;
			case rendering::TextureSlot::Specular: flag = MatFlag_HasSpecular; break;
			case rendering::TextureSlot::Emissive: flag = MatFlag_HasEmissive; break;
			default: return;
			}
			SetMaterialFlag(flag, resource != nullptr);
		}

		void StaticMesh::SetTextureOverride(rendering::TextureSlot slot, std::shared_ptr<IShaderResourceView> srv)
		{
			textureOverrides_[static_cast<uint32_t>(slot)] = std::move(srv);
		}


		void StaticMesh::ClearTextureOverride(rendering::TextureSlot slot)
		{
			textureOverrides_[static_cast<uint32_t>(slot)].reset();
		}

		bool StaticMesh::FillRenderItem(rendering::RenderItem& item) const
		{
			// PBR 系は pbrMaterialCB_ を GPU に送る（memcpy で型安全に転送）
			const bool isPBR = shaderType_ == ShaderType::PBRLit ||
			                   shaderType_ == ShaderType::TerrainPBRLit;
			const bool ok = isPBR
				? FillRenderItemBase(item, isInitialized_,
				      vsShaderResource_, psShaderResource_,
				      vertexBuffer_, indexBuffer_, samplerState_,
				      gpuResources_, indicesSize_, worldMatrix_, pbrMaterialCB_)
				: FillRenderItemBase(item, isInitialized_,
				      vsShaderResource_, psShaderResource_,
				      vertexBuffer_, indexBuffer_, samplerState_,
				      gpuResources_, indicesSize_, worldMatrix_, materialCB_);
			if (!ok)
				return false;

			for (uint32_t slot = 0; slot < static_cast<uint32_t>(rendering::TextureSlot::Count); ++slot)
			{
				if (textureOverrides_[slot])
					item.textures[slot] = textureOverrides_[slot];
			}

			item.castShadow    = castShadow_;
			item.receiveShadow = receiveShadow_;

			// カリング用ローカル AABB (MeshResource 経由でロードした場合のみ)。
			// 生バッファ初期化 (InitializeDynamic 等) では meshResource_ が無いため
			// hasBounds = false のままとし、フラスタムカリングの対象外とする。
			if (meshResource_)
			{
				item.localBounds = meshResource_->GetLocalAABB();
				item.hasBounds   = true;

				// クラスタカリング用データ (リソースを alias して寿命を繋ぐ)
				if (cullIndexBuffer_)
				{
					item.cullIndexBuffer  = cullIndexBuffer_;
					item.clusters         = std::shared_ptr<const std::vector<MeshCluster>>(
						meshResource_, &meshResource_->GetClusters());
					item.reorderedIndices = std::shared_ptr<const std::vector<uint32_t>>(
						meshResource_, &meshResource_->GetReorderedIndices());
				}
				// GPU 駆動クラスタカリング用バッファ
				if (gpuClusterBuffers_.Valid())
				{
					item.gpuClusters   = gpuClusterBuffers_.clusters;
					item.gpuSrcIndices = gpuClusterBuffers_.srcIndices;
					item.gpuOutIndices = gpuClusterBuffers_.outIndices;
					item.gpuArgs       = gpuClusterBuffers_.args;
					item.clusterCount  = gpuClusterBuffers_.clusterCount;
				}
			}

			// ディファード対象なら G-Buffer PS をセット（エイリアシングで寿命を繋げる）
			if (gbufferPSShaderResource_ && gbufferPSShaderResource_->IsCompleted())
			{
				IShader* rawPS = gbufferPSShaderResource_->GetShader();
				item.gbufferPS = rawPS
					? std::shared_ptr<IShader>(gbufferPSShaderResource_, rawPS)
					: nullptr;
			}
			else
			{
				item.gbufferPS.reset();
			}

			// TerrainLit / TerrainPBRLit は t1 (layer0/grass) を必ず sample するため、
			// t1 が未ロードなら描画をスキップする。HasSplatMap の有無に依存しない。
			if (shaderType_ == ShaderType::TerrainLit || shaderType_ == ShaderType::TerrainPBRLit)
			{
				if (!item.textures[static_cast<uint32_t>(rendering::TextureSlot::Normal)])
					return false;
			}

			// HasSplatMap が要求されている場合: t0/t2/t3 のいずれかが未ロードなら
			// フラグを draw item だけ落として grass 単色で安全描画。
			// materialCB_ 本体は変更せず、次フレームに SRV が揃えば自動的に復活する。
			if (materialCB_.flags & static_cast<uint32_t>(MatFlag_HasSplatMap))
			{
				const bool t0 = item.textures[static_cast<uint32_t>(rendering::TextureSlot::Albedo)]   != nullptr;
				const bool t2 = item.textures[static_cast<uint32_t>(rendering::TextureSlot::Specular)] != nullptr;
				const bool t3 = item.textures[static_cast<uint32_t>(rendering::TextureSlot::Emissive)] != nullptr;
				if (!t0 || !t2 || !t3)
					item.materialCB.flags &= ~static_cast<uint32_t>(MatFlag_HasSplatMap);
			}
			return true;
		}
	}
}
