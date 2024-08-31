#pragma once
#include "GPUBuffer.h"
#include "Shader.h"

namespace engine
{
	namespace graphics
	{
		/**
		 * �T���v���X�e�[�g
		 */
		class SamplerState
		{
		private:
			ID3D11SamplerState* samplerState_;


		public:
			SamplerState();
			~SamplerState();

			/** �T���v���X�e�[�g���� */
			bool Create(const D3D11_SAMPLER_DESC& desc);
			/** ��� */
			void Release();
			/** �T���v���X�e�[�g�擾 */
			ID3D11SamplerState*& GetBody() { return samplerState_; }
		};




		/*******************************************/


		/**
		 * ShaderResourceView
		 * NOTE: �e�N�X�`����X�g���N�`���[�h�o�b�t�@�ȂǁA�V�F�[�_�[�Ŏg�p���郊�\�[�X�r���[
		 */
		class ShaderResourceView
		{
		private:
			ID3D11ShaderResourceView* shaderResourceView_;


		public:
			ShaderResourceView();
			~ShaderResourceView();

			/** StructuredBuffer�p��SRV���� */
			bool Create(StructuredBuffer& structuredBuffer);
			/** �e�N�X�`���p��SRV���� */
			bool Create(ID3D11Texture2D* texture);
			/** ��� */
			void Release();
			/** ShaderResourceView�擾 */
			inline ID3D11ShaderResourceView*& GetBody() { return shaderResourceView_; }
		};




		/*******************************************/


		/**
		 * UnorderedAccessView
		 * NOTE: �R���s���[�g�V�F�[�_�[�ƃs�N�Z���V�F�[�_�[�̏o�͂Ɏg�p����r���[
		 */
		class UnorderedAccessView
		{
		private:
			ID3D11UnorderedAccessView* unorderedAccessView_;


		public:
			UnorderedAccessView();
			~UnorderedAccessView();

			/** StructuredBuffer�p��UAV���� */
			bool Create(StructuredBuffer& structuredBuffer);
			/** �e�N�X�`���p��UAV���� */
			bool Create(ID3D11Texture2D* texture);
			/** ��� */
			void Release();
			/** UnorderedAccessView�擾 */
			inline ID3D11UnorderedAccessView*& GetBody() { return unorderedAccessView_; }
		};




		/*******************************************/


		/**
		 * �����_�����O�^�[�Q�b�g
		 */
		class RenderTarget
		{
		private:
			ID3D11Texture2D* renderTarget_;
			ID3D11RenderTargetView* renderTargetView_;
			ID3D11Texture2D* depthStencil_;
			ID3D11DepthStencilView* depthStencilView_;
			ShaderResourceView renderTargetSRV_;
			UnorderedAccessView renderTargetUAV_;

		public:
			RenderTarget();
			~RenderTarget();

			/** �����_�����O�^�[�Q�b�g���� */
			bool Create(int32_t width, int32_t height, int32_t mipLevel, DXGI_FORMAT colorFormat, DXGI_FORMAT depthStencilFormat, DXGI_SAMPLE_DESC multiSampleDesc, ID3D11Texture2D* renderTarget = nullptr, ID3D11Texture2D* depthStencil = nullptr);
			/** ��� */
			void Release();

			/** �����_�����O�^�[�Q�b�g�擾 */
			inline ID3D11Texture2D* GetRenderTarget() const
			{
				return renderTarget_;
			}
			/** �����_�����O�^�[�Q�b�g�r���[�擾 */
			inline ID3D11RenderTargetView* GetrenderTargetView() const
			{
				return renderTargetView_;
			}
			/** �����_�����O�^�[�Q�b�gSRV�擾 */
			inline ShaderResourceView& GetRenderTargetSRV()
			{
				return renderTargetSRV_;
			}
			/** �����_�����O�^�[�Q�b�gUAV�擾 */
			inline UnorderedAccessView& GetRenderTargetUAV()
			{
				return renderTargetUAV_;
			}
			/** �f�v�X�X�e���V���r���[�擾 */
			inline ID3D11DepthStencilView* GetDepthStencilView() const
			{
				return depthStencilView_;
			}
		};



		/*******************************************/


		class RenderContext
		{
		private:
			static constexpr uint32_t MAX_MRT_NUM = 8;


		private:
			/** �f�o�C�X�R���e�L�X�g */
			ID3D11DeviceContext* d3dDeviceContext_;
			/** �r���[�|�[�g */
			D3D11_VIEWPORT viewport_;
			/** ���ݎg�p���̃����_�����O�^�[�Q�b�g�r���[ */
			ID3D11RenderTargetView* renderTargetViews_[MAX_MRT_NUM];
			/** ���ݐݒ肳��Ă���f�v�X�X�e���V���r���[ */
			ID3D11DepthStencilView* depthStencilView_;
			/** �����_�����O�^�[�Q�b�g�r���[�̐� */
			uint32_t renderTargetViewNum_;


		public:
			RenderContext();
			~RenderContext();

			/** ������ */
			void Initialize(ID3D11DeviceContext* d3dDeviceContext);
			/** �����_�����O�^�[�Q�b�g�r���[�ݒ� */
			void OMSetRenderTargets(uint32_t numViews, RenderTarget* renderTarget);
			/** �r���[�|�[�g�ݒ� */
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height)
			{
				viewport_.Width = width;
				viewport_.Height = height;
				viewport_.TopLeftX = topLeftX;
				viewport_.TopLeftY = topLeftY;
				viewport_.MinDepth = 0.0f;
				viewport_.MaxDepth = 1.0f;
				d3dDeviceContext_->RSSetViewports(1, &viewport_);
			}
			/** ���X�^���C�U�ݒ� */
			void RSSetState(ID3D11RasterizerState* state)
			{
				d3dDeviceContext_->RSSetState(state);
			}
			/** �����_�����O�^�[�Q�b�g�N���A */
			void ClearRenderTargetView(uint32_t index, float* clearColor)
			{
				if (renderTargetViews_ && index < renderTargetViewNum_) {
					d3dDeviceContext_->ClearRenderTargetView(renderTargetViews_[index], clearColor);
					d3dDeviceContext_->ClearDepthStencilView(depthStencilView_, D3D11_CLEAR_DEPTH, 1.0f, 0);
				}
			}
			/** ���_�o�b�t�@�ݒ� */
			void IASetVertexBuffer(VertexBuffer& vertexBuffer)
			{
				uint32_t offset = 0;
				uint32_t stride = vertexBuffer.GetStride();
				d3dDeviceContext_->IASetVertexBuffers(0, 1, &vertexBuffer.GetBody(), &stride, &offset);
			}
			/** �C���f�b�N�X�o�b�t�@�ݒ� */
			void IASetIndexBuffer(IndexBuffer& indexBuffer)
			{
				d3dDeviceContext_->IASetIndexBuffer(indexBuffer.GetBody(), DXGI_FORMAT_R32_UINT, 0);
			}
			/** �v���~�e�B�u�g�|���W�[�ݒ� */
			void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology)
			{
				d3dDeviceContext_->IASetPrimitiveTopology(topology);
			}
			/** VS�X�e�[�W�ɒ萔�o�b�t�@�ݒ� */
			void VSSetConstantBuffer(uint32_t startSlot, ConstantBuffer& constantBuffer)
			{
				d3dDeviceContext_->VSSetConstantBuffers(startSlot, 1, &constantBuffer.GetBody());
			}
			/** PS�X�e�[�W�ɒ萔�o�b�t�@�ݒ� */
			void PSSetConstantBuffer(uint32_t startSlot, ConstantBuffer& constantBuffer)
			{
				d3dDeviceContext_->PSSetConstantBuffers(startSlot, 1, &constantBuffer.GetBody());
			}
			/** PS�X�e�[�W��SRV�ݒ� */
			void PSSetShaderResource(uint32_t startSlot, ShaderResourceView& shaderResourceView)
			{
				d3dDeviceContext_->PSSetShaderResources(startSlot, 1, &shaderResourceView.GetBody());
			}
			/** PS�X�e�[�W����SRV���O�� */
			void PSUnsetShaderResource(uint32_t slot)
			{
				ID3D11ShaderResourceView* view[] = {
					nullptr,
				};
				d3dDeviceContext_->PSSetShaderResources(slot, 1, view);
			}
			/** PS�X�e�[�W�ɃT���v���X�e�[�g�ݒ� */
			void PsSetSampler(uint32_t startSlot, SamplerState& samplerState)
			{
				d3dDeviceContext_->PSSetSamplers(startSlot, 1, &samplerState.GetBody());
			}
			/** ���_�V�F�[�_�[�ݒ� */
			void VSSetShader(Shader& shader)
			{
				d3dDeviceContext_->VSSetShader((ID3D11VertexShader*)shader.GetBody(), nullptr, 0);
			}
			/** �s�N�Z���V�F�[�_�[�ݒ� */
			void PSSetShader(Shader& shader)
			{
				d3dDeviceContext_->PSSetShader((ID3D11PixelShader*)shader.GetBody(), nullptr, 0);
			}
			/** �R���s���[�g�V�F�[�_�[�ݒ� */
			void CSSetShader(Shader& shader)
			{
				d3dDeviceContext_->CSSetShader((ID3D11ComputeShader*)shader.GetBody(), nullptr, 0);
			}
			/** CS�X�e�[�W�ɒ萔�o�b�t�@�ݒ� */
			void CSSetConstantBuffer(uint32_t startSlot, ConstantBuffer& constantBuffer)
			{
				d3dDeviceContext_->CSSetConstantBuffers(startSlot, 1, &constantBuffer.GetBody());
			}
			/** �R���s���[�g�V�F�[�_�[��SRV�ݒ� */
			void CSSetShaderResource(uint32_t startSlot, ShaderResourceView& shaderResourceView)
			{
				d3dDeviceContext_->CSSetShaderResources(startSlot, 1, &shaderResourceView.GetBody());
			}
			/** �R���s���[�g�V�F�[�_�[����SRV���O�� */
			void CSUnsetShaderResource(uint32_t slot)
			{
				ID3D11ShaderResourceView* view[] = {
					nullptr,
				};
				d3dDeviceContext_->CSSetShaderResources(slot, 1, view);
			}
			/** �R���s���[�g�V�F�[�_�[��UAV�ݒ� */
			void CSSetUnorderedAccessView(uint32_t startSlot, UnorderedAccessView& unorderedAccessView)
			{
				d3dDeviceContext_->CSSetUnorderedAccessViews(startSlot, 1, &unorderedAccessView.GetBody(), nullptr);
			}
			/** �R���s���[�g�V�F�[�_�[����UAV���O�� */
			void CSUnsetUnorderedAccessView(uint32_t slot)
			{
				ID3D11UnorderedAccessView* view[] = {
					nullptr,
				};
				d3dDeviceContext_->CSSetUnorderedAccessViews(slot, 1, view, nullptr);
			}
			/** �`�� */
			void Draw(uint32_t vertexCount, uint32_t startVertexLocation)
			{
				d3dDeviceContext_->Draw(vertexCount, startVertexLocation);
			}
			void DrawIndexed(uint32_t indexCount)
			{
				d3dDeviceContext_->DrawIndexed(indexCount, 0, 0);
			}
			/** �f�B�X�p�b�` */
			void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				d3dDeviceContext_->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			}
			/** ���̓��C�A�E�g�ݒ� */
			void IASetInputLayout(ID3D11InputLayout* inputLayout)
			{
				d3dDeviceContext_->IASetInputLayout(inputLayout);
			}
			/** ���\�[�X�R�s�[ */
			template <typename TResource>
			void CopyResource(TResource& destResource, TResource& srcResource)
			{
				if (destResource.GetBody() && srcResource.GetBody()) {
					d3dDeviceContext_->CopyResource(destResource.GetBody(), srcResource.GetBody());
				}
			}
			void CopyResource(ID3D11Resource* destResource, ID3D11Resource* srcResource)
			{
				if (destResource && srcResource) {
					d3dDeviceContext_->CopyResource(destResource, srcResource);
				}
			}
			/** �}�b�v */
			template <typename TBuffer>
			void Map(TBuffer& buffer, uint32_t subResource, D3D11_MAP mapType, uint32_t mapFlags, D3D11_MAPPED_SUBRESOURCE& mappedResource)
			{
				if (buffer.GetBody()) {
					d3dDeviceContext_->Map(buffer.GetBody(), subResource, mapType, mapFlags, &mappedResource);
				}
			}
			template <typename TBuffer>
			void Unmap(TBuffer& buffer, uint32_t subResource)
			{
				if (buffer.GetBody()) {
					d3dDeviceContext_->Unmap(buffer.GetBody(), subResource);
				}
			}
			/** �T�u���\�[�X�X�V */
			template <typename TBuffer, typename SrcBuffer>
			void UpdateSubresource(TBuffer& gpuBuffer, SrcBuffer buffer)
			{
				if (gpuBuffer.GetBody()) {
					d3dDeviceContext_->UpdateSubresource(gpuBuffer.GetBody(), 0, nullptr, &buffer, 0, 0);
				}
			}
		};




		/*******************************************/


		class Texture
		{
		public:
			static graphics::ShaderResourceView* Create2D(const DirectX::TexMetadata& metaData, const DirectX::Image* images);
		};
	}
}