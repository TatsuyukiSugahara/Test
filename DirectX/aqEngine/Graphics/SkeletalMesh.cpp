#include "aq.h"
#include "SkeletalMesh.h"
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
			// SkeletalMesh 用シェーダー一覧
			// TKM v101 (ボーンあり) 専用シェーダー。頂点レイアウトに BLENDWEIGHTS / BLENDINDICES が必要。
			ShaderInformation skeletalShaderInformations[] = {
				{ "Assets/Shader/SkeletalModelLit.fx", "VSMain", "Assets/Shader/SkeletalModelLit.fx", "PSMain",
				  nullptr, nullptr },                                                                               // SkeletalModelLit (forward-only)
				{ "Assets/Shader/SkeletalModelLit.fx", "VSMain", "Assets/Shader/SkeletalModelLit.fx", "PSMain",
				  "Assets/Shader/SkeletalPBRGBuffer.fx", "PSMain" },                                              // SkeletalPBRLit
			};
		}


		SkeletalMesh::SkeletalMesh()
			: indicesSize_(0)
			, worldMatrix_(math::Matrix4x4::Identity)
			, localMatrix_(math::Matrix4x4::Identity)
			, isInitialized_(false)
		{
		}


		SkeletalMesh::~SkeletalMesh()
		{
		}


		void SkeletalMesh::Initialize(res::RefSkeletalMeshResource meshResource,
		                               res::RefGPUResource          albedoResource,
		                               ShaderType                   shaderType)
		{
			skeletalMeshResource_ = meshResource;
			gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Albedo)] = albedoResource;
			isInitialized_ = false;

			if (!skeletalMeshResource_ ||
			    skeletalMeshResource_->GetVertexCount() == 0 ||
			    skeletalMeshResource_->GetIndexCount()  == 0)
			{
				return;
			}

			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(
				skeletalMeshResource_->GetVertexCount(),
				sizeof(graphics::SkinnedVertexData),
				skeletalMeshResource_->GetVertices().data()
			);
			indexBuffer_ = GraphicsDevice::Get().CreateIndexBuffer(
				skeletalMeshResource_->GetIndexCount(),
				skeletalMeshResource_->GetIndices().data()
			);
			indicesSize_ = skeletalMeshResource_->GetIndexCount();

			// クラスタカリング用: 並べ替えインデックスで動的IBを作る (CPU 方式の compact 宛先)
			const std::vector<uint32_t>& reordered = skeletalMeshResource_->GetReorderedIndices();
			if (!reordered.empty())
			{
				cullIndexBuffer_ = GraphicsDevice::Get().CreateDynamicIndexBuffer(
					static_cast<uint32_t>(reordered.size()), IndexFormat::UInt32, reordered.data());
			}
			// GPU 駆動クラスタカリング用バッファ
			gpuClusterBuffers_.Create(skeletalMeshResource_->GetClusters(), reordered);

			// バインドポーズ (単位行列) でボーン行列を初期化
			const uint32_t boneCount = std::max(skeletalMeshResource_->GetBoneCount(), 1u);
			boneMatrices_ = std::make_shared<std::vector<math::Matrix4x4>>(
				boneCount, math::Matrix4x4::Identity);

			Initialize(shaderType);
		}


		void SkeletalMesh::Initialize(ShaderType shaderType)
		{
			shaderType_ = shaderType;
			const ShaderInformation& info = skeletalShaderInformations[static_cast<uint8_t>(shaderType)];
			vsShaderResource_ = res::ResourceManager::Get().LoadShader(
				info.vsFilePath, info.vsFuncName, IShader::ShaderType::VS);
			psShaderResource_ = res::ResourceManager::Get().LoadShader(
				info.psFilePath, info.psFuncName, IShader::ShaderType::PS);

			// G-Buffer PS はディファード対象シェーダーのみロード（パスが nullptr なら forward-only）
			if (info.gbufferPSFilePath != nullptr)
			{
				gbufferPSShaderResource_ = res::ResourceManager::Get().LoadShader(
					info.gbufferPSFilePath, info.gbufferPSFuncName, IShader::ShaderType::PS);
			}
			else
			{
				gbufferPSShaderResource_.reset();
			}

			const bool hasAlbedo =
				gpuResources_[static_cast<uint32_t>(rendering::TextureSlot::Albedo)] != nullptr;
			if (hasAlbedo) {
				SamplerDesc samplerDesc;
				samplerDesc.filter   = FilterMode::MinMagMipLinear;
				samplerDesc.addressU = AddressMode::Clamp;
				samplerDesc.addressV = AddressMode::Clamp;
				samplerDesc.addressW = AddressMode::Clamp;
				samplerState_ = GraphicsDevice::Get().CreateSamplerState(samplerDesc);
			}

			isInitialized_ = vertexBuffer_ && indexBuffer_;
		}


		void SkeletalMesh::SetLocalMatrix(const math::Matrix4x4& localMatrix)
		{
			localMatrix_ = localMatrix;
		}


		void SkeletalMesh::Update(const math::Vector3& translation,
		                           const math::Quaternion& rotation,
		                           const math::Vector3& scale)
		{
			math::Matrix4x4 scaleMatrix, rotationMatrix, translationMatrix,
			                localScaleMatrix, localRotationMatrix;
			scaleMatrix.MakeScaling(scale);
			rotationMatrix.MakeRotationFromQuaternion(rotation);
			translationMatrix.MakeTranslation(translation);
			localScaleMatrix.Mull(localMatrix_, scaleMatrix);
			localRotationMatrix.Mull(localScaleMatrix, rotationMatrix);
			worldMatrix_.Mull(localRotationMatrix, translationMatrix);
		}


		void SkeletalMesh::SetTexture(rendering::TextureSlot slot, res::RefGPUResource resource)
		{
			gpuResources_[static_cast<uint32_t>(slot)] = resource;

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


		bool SkeletalMesh::FillRenderItem(rendering::RenderItem& item) const
		{
			const bool isPBR = shaderType_ == ShaderType::SkeletalPBRLit;
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

			item.castShadow    = castShadow_;
			item.receiveShadow = receiveShadow_;
			item.boneMatrices  = boneMatrices_;

			// カリング用バウンディング。バインドポーズ AABB にアニメ変形分の
			// マージン (extent を 1.5 倍) を付けて保守的にする。
			if (skeletalMeshResource_)
			{
				constexpr float ANIM_MARGIN = 1.5f;
				const math::AABB& bind = skeletalMeshResource_->GetLocalAABB();
				item.localBounds = math::AABB(bind.center,
					math::Vector3(bind.extent.x * ANIM_MARGIN,
					              bind.extent.y * ANIM_MARGIN,
					              bind.extent.z * ANIM_MARGIN));
				item.hasBounds = true;

				// クラスタカリング用データ (リソースを alias して寿命を繋ぐ)
				if (cullIndexBuffer_)
				{
					item.cullIndexBuffer  = cullIndexBuffer_;
					item.clusters         = std::shared_ptr<const std::vector<MeshCluster>>(
						skeletalMeshResource_, &skeletalMeshResource_->GetClusters());
					item.reorderedIndices = std::shared_ptr<const std::vector<uint32_t>>(
						skeletalMeshResource_, &skeletalMeshResource_->GetReorderedIndices());
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

			return true;
		}


		uint32_t SkeletalMesh::GetBoneCount() const
		{
			return skeletalMeshResource_ ? skeletalMeshResource_->GetBoneCount() : 0;
		}


		const std::vector<res::BoneData>* SkeletalMesh::GetBones() const
		{
			return skeletalMeshResource_ ? &skeletalMeshResource_->GetBones() : nullptr;
		}
	}
}
