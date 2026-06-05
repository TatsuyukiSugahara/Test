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
		{
		}


		StaticMesh::~StaticMesh()
		{
		}


		void StaticMesh::Initialize(engine::res::RefMeshResource meshResource, engine::res::RefGPUResource gpuResource, const ShaderType shaderType)
		{
			meshResource_ = meshResource;
			gpuResource_  = gpuResource;

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
			vertexBuffer_ = GraphicsDevice::Get().CreateVertexBuffer(vertexNum, sizeof(engine::graphics::VertexData), vertexBuffer);
			indexBuffer_  = GraphicsDevice::Get().CreateIndexBuffer(indexNum, indexBuffer);
			indicesSize_  = indexNum;

			Initialize(shaderType);
		}


		void StaticMesh::Initialize(const ShaderType shaderType)
		{
			const ShaderInformation& info = shaderInformations[static_cast<uint8_t>(shaderType)];
			vsShader_ = GraphicsDevice::Get().CreateShader(info.vsFilePath, info.vsFuncName, IShader::ShaderType::VS);
			psShader_ = GraphicsDevice::Get().CreateShader(info.psFilePath, info.psFuncName, IShader::ShaderType::PS);

			if (gpuResource_) {
				SamplerDesc samplerDesc;
				samplerDesc.filter   = FilterMode::MinMagMipLinear;
				samplerDesc.addressU = AddressMode::Clamp;
				samplerDesc.addressV = AddressMode::Clamp;
				samplerDesc.addressW = AddressMode::Clamp;
				samplerState_ = GraphicsDevice::Get().CreateSamplerState(samplerDesc);
			}

			constantBuffer_ = GraphicsDevice::Get().CreateConstantBuffer(nullptr, sizeof(engine::graphics::VSConstantBuffer));
		}


		void StaticMesh::Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale)
		{
			math::Matrix4x4 scaleMatrix, rotationMatrix, translationMatrix;
			scaleMatrix.MakeScaling(scale);
			rotationMatrix.MakeRotationFromQuaternion(rotation);
			translationMatrix.MakeTranslation(translation);
			worldMatrix_.Mull(scaleMatrix, rotationMatrix);
			worldMatrix_.Mull(worldMatrix_, translationMatrix);
		}


		void StaticMesh::Render(RenderContext& context, const math::Matrix4x4& view, const math::Matrix4x4& projection)
		{
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

			if (gpuResource_) {
				context.PSSetShaderResource(0, *gpuResource_->GetShaderResourceView());
				context.PsSetSampler(0, *samplerState_);
			}

			context.VSSetShader(*vsShader_);
			context.PSSetShader(*psShader_);
			context.IASetInputLayout(*vsShader_);

			context.DrawIndexed(indicesSize_);
		}
	}
}
