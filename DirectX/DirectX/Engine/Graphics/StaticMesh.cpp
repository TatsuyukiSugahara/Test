#include "../EnginePreCompile.h"
#include "StaticMesh.h"
#include "GraphicsDevice.h"


namespace engine
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
			ShaderInformation shaderInformations[] = {
				{ "Assets/Shader/Model.fx",     "VSMain", "Assets/Shader/Model.fx",     "PSMain" },
				{ "Assets/Shader/SimpleBox.fx", "VSMain", "Assets/Shader/SimpleBox.fx", "PSMain" },
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


		void StaticMesh::Initialize(engine::res::RefMeshResource meshResource, engine::res::RefGPUResource gpuResource, const ShaderType shaderType)
		{
			meshResource_ = meshResource;
			gpuResource_  = gpuResource;
			isInitialized_ = false;

			if (!meshResource_ || meshResource_->GetVerticsSize() == 0 || meshResource_->GetIndicesSize() == 0) {
				return;
			}

			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(
				meshResource_->GetVerticsSize(),
				sizeof(engine::graphics::VertexData),
				meshResource_->GetVertics()->data()
			);
			indexBuffer_ = GraphicsDevice::Get().CreateIndexBuffer(
				meshResource_->GetIndicesSize(),
				meshResource_->GetIndices()->data()
			);
			indicesSize_ = meshResource_->GetIndicesSize();

			Initialize(shaderType);
		}


		void StaticMesh::Initialize(const void* vertexBuffer, const uint32_t vertexNum, const void* indexBuffer, const uint32_t indexNum, const ShaderType shaderType)
		{
			isInitialized_ = false;
			if (!vertexBuffer || vertexNum == 0 || !indexBuffer || indexNum == 0) {
				return;
			}

			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(vertexNum, sizeof(engine::graphics::VertexData), vertexBuffer);
			indexBuffer_  = GraphicsDevice::Get().CreateIndexBuffer(indexNum, indexBuffer);
			indicesSize_  = indexNum;

			Initialize(shaderType);
		}


		void StaticMesh::Initialize(const ShaderType shaderType)
		{
			const ShaderInformation& info = shaderInformations[static_cast<uint8_t>(shaderType)];
			vsShaderResource_ = engine::res::ResourceManager::Get().LoadShader(info.vsFilePath, info.vsFuncName, IShader::ShaderType::VS);
			psShaderResource_ = engine::res::ResourceManager::Get().LoadShader(info.psFilePath, info.psFuncName, IShader::ShaderType::PS);

			if (gpuResource_) {
				SamplerDesc samplerDesc;
				samplerDesc.filter   = FilterMode::MinMagMipLinear;
				samplerDesc.addressU = AddressMode::Clamp;
				samplerDesc.addressV = AddressMode::Clamp;
				samplerDesc.addressW = AddressMode::Clamp;
				samplerState_ = GraphicsDevice::Get().CreateSamplerState(samplerDesc);
			}

			constantBuffer_ = GraphicsDevice::Get().CreateConstantBuffer(nullptr, sizeof(engine::graphics::VSConstantBuffer));
			isInitialized_ = vertexBuffer_ && indexBuffer_ && constantBuffer_;
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


		void StaticMesh::Render(RenderContext& context, const math::Matrix4x4& view, const math::Matrix4x4& projection)
		{
			if (!isInitialized_) {
				return;
			}
			if (!vsShaderResource_ || !psShaderResource_) {
				return;
			}
			if (!vsShaderResource_->IsCompleted() || !psShaderResource_->IsCompleted()) {
				return;
			}

			IShader* vsShader = vsShaderResource_->GetShader();
			IShader* psShader = psShaderResource_->GetShader();
			if (!vsShader || !psShader) {
				return;
			}

			VSConstantBuffer cb;
			cb.world      = worldMatrix_;
			cb.view       = view;
			cb.projection = projection;
			context.UpdateSubresource(*constantBuffer_, cb);
			context.VSSetConstantBuffer(0, *constantBuffer_);
			context.PSSetConstantBuffer(0, *constantBuffer_);

			context.IASetVertexBuffer(*vertexBuffer_);
			context.IASetIndexBuffer(*indexBuffer_);
			context.IASetPrimitiveTopology(PrimitiveTopology::TriangleList);

			if (gpuResource_ && gpuResource_->GetShaderResourceView() && samplerState_) {
				context.PSSetShaderResource(0, *gpuResource_->GetShaderResourceView());
				context.PsSetSampler(0, *samplerState_);
			}

			context.VSSetShader(*vsShader);
			context.PSSetShader(*psShader);
			context.IASetInputLayout(*vsShader);

			context.DrawIndexed(indicesSize_);
		}
	}
}
