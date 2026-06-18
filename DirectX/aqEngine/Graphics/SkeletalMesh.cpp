#include "aq.h"
#include "SkeletalMesh.h"
#include "GraphicsDevice.h"
#include <algorithm>


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
			};
			// SkeletalMesh 用シェーダー一覧
			// TKM v101 (ボーンあり) 専用シェーダー。頂点レイアウトに BLENDWEIGHTS / BLENDINDICES が必要。
			ShaderInformation skeletalShaderInformations[] = {
				{ "Assets/Shader/SkeletalModelLit.fx", "VSMain", "Assets/Shader/SkeletalModelLit.fx", "PSMain" },
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
			if (resource) materialCB_.flags |=  static_cast<uint32_t>(flag);
			else          materialCB_.flags &= ~static_cast<uint32_t>(flag);
		}


		bool SkeletalMesh::FillRenderItem(rendering::RenderItem& item) const
		{
			if (!isInitialized_)                         return false;
			if (!vsShaderResource_ || !psShaderResource_) return false;
			if (!vsShaderResource_->IsCompleted() ||
			    !psShaderResource_->IsCompleted())         return false;

			IShader* vs = vsShaderResource_->GetShader();
			IShader* ps = psShaderResource_->GetShader();
			if (!vs || !ps) return false;

			item.vertexBuffer = vertexBuffer_;
			item.indexBuffer  = indexBuffer_;
			item.samplerState = samplerState_;
			item.vs = std::shared_ptr<IShader>(vsShaderResource_, vs);
			item.ps = std::shared_ptr<IShader>(psShaderResource_, ps);

			constexpr uint32_t kCount = static_cast<uint32_t>(rendering::TextureSlot::Count);
			for (uint32_t i = 0; i < kCount; ++i) {
				const auto& gpuRes = gpuResources_[i];
				if (gpuRes) {
					IShaderResourceView* srv = gpuRes->GetShaderResourceView();
					item.textures[i] = srv
						? std::shared_ptr<IShaderResourceView>(gpuRes, srv)
						: nullptr;
				} else {
					item.textures[i] = nullptr;
				}
			}

			item.indexCount    = indicesSize_;
			item.worldMatrix   = worldMatrix_;
			item.layer         = 0;
			item.materialCB    = materialCB_;
			item.boneMatrices  = boneMatrices_;

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
