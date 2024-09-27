#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "StaticMesh.h"

namespace engine
{
	namespace graphics
	{
		namespace
		{
			struct ShaderInformation
			{
				const char* vsFileNme;
				const char* vsFuncName;
				const char* psFileNme;
				const char* psFuncName;
			};
			ShaderInformation shaderInformations[] = {
				{ "Assets/Shader/Model.fx", "VSMain", "Assets/Shader/Model.fx", "PSMain"},
			};
		}


		StaticMesh::StaticMesh()
			: worldMatrix_(math::Matrix4x4::Identity)
			, indicesSize_(0)
		{
		}


		StaticMesh::~StaticMesh()
		{
		}


		void StaticMesh::Initialize(engine::res::RefMeshResource meshResource, engine::res::RefGPUResource gpuResource, const ShaderType shaderType)
		{
			meshResource_ = meshResource;
			gpuResource_ = gpuResource;

			vertexBuffer_.Create(meshResource_->GetVerticsSize(), sizeof(engine::graphics::VertexData), meshResource_->GetVertics()->data());
			indexBuffer_.Create(meshResource_->GetIndicesSize(), meshResource_->GetIndices()->data());
			indicesSize_ = meshResource_->GetIndicesSize();

			Initialize(shaderType);
		}


		void StaticMesh::Initialize(const void* vertexBuffer, const uint32_t vertexNum, const void* indexBuffer, const uint32_t indexNum, const ShaderType shaderType)
		{
			vertexBuffer_.Create(vertexNum, sizeof(engine::graphics::VertexData), vertexBuffer);
			indexBuffer_.Create(indexNum, indexBuffer);
			indicesSize_ = indexNum;

			Initialize(shaderType);
		}


		void StaticMesh::Initialize(const ShaderType shaderType)
		{
			// 使用するシェーダーを設定
			const ShaderInformation& shaderInformation = shaderInformations[static_cast<uint8_t>(shaderType)];
			vsShader_.Load(shaderInformation.vsFileNme, shaderInformation.vsFuncName, engine::graphics::Shader::ShaderType::VS);
			psShader_.Load(shaderInformation.psFileNme, shaderInformation.psFuncName, engine::graphics::Shader::ShaderType::PS);

			// 使用するテクスチャのサンプラー設定
			D3D11_SAMPLER_DESC samplerDesc;
			engine::memory::Clear(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerState_.Create(samplerDesc);

			// 定数バッファ生成
			constantBuffer_.Create(nullptr, sizeof(engine::graphics::VSConstantBuffer));
		}


		void StaticMesh::Update(const math::Vector3& translation, const math::Quaternion& rotation, const math::Vector3& scale)
		{
			math::Matrix4x4 scaleMatrix, rotationMatrix, translationMatrix;
			scaleMatrix.MakeScaling(scale);
			rotationMatrix.MakeRotationFromQuaternion(rotation);
			translationMatrix.MakeTranslation(translation);
			// 拡縮*回転*平行移動
			worldMatrix_.Mull(scaleMatrix, rotationMatrix);
			worldMatrix_.Mull(worldMatrix_, translationMatrix);
		}


		void StaticMesh::Render(RenderContext& context, const math::Matrix4x4& view, const math::Matrix4x4& projection)
		{
			// 定数バッファ更新
			VSConstantBuffer cb;
			cb.world = worldMatrix_;
			cb.view = view;
			cb.projection = projection;
			context.UpdateSubresource(constantBuffer_, cb);
			context.VSSetConstantBuffer(0, constantBuffer_);
			context.PSSetConstantBuffer(0, constantBuffer_);


			// 描画関連の設定
			float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			context.ClearRenderTargetView(0, clearColor);

			context.IASetVertexBuffer(vertexBuffer_);
			context.IASetIndexBuffer(indexBuffer_);
			context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			if (gpuResource_) {
				context.PSSetShaderResource(0, *gpuResource_->GetShaderResourceView());
			}
			context.PsSetSampler(0, samplerState_);

			context.VSSetShader(vsShader_);
			context.PSSetShader(psShader_);
			context.IASetInputLayout(vsShader_.GetInputLayout());

			context.DrawIndexed(indicesSize_);
		}
	}
}