#pragma once
#include "GPUBuffer.h"
#include "Shader.h"

namespace engine
{
	namespace graphics
	{
		/**
		 * サンプラステート
		 */
		class SamplerState
		{
		private:
			ID3D11SamplerState* samplerState_;


		public:
			SamplerState();
			~SamplerState();

			/** サンプラステート生成 */
			bool Create(const D3D11_SAMPLER_DESC& desc);
			/** 解放 */
			void Release();
			/** サンプラステート取得 */
			ID3D11SamplerState*& GetBody() { return samplerState_; }
		};




		/*******************************************/


		/**
		 * ShaderResourceView
		 * NOTE: テクスチャやストラクチャードバッファなど、シェーダーで使用するリソースビュー
		 */
		class ShaderResourceView
		{
		private:
			ID3D11ShaderResourceView* shaderResourceView_;


		public:
			ShaderResourceView();
			~ShaderResourceView();

			/** StructuredBuffer用のSRV生成 */
			bool Create(StructuredBuffer& structuredBuffer);
			/** テクスチャ用のSRV生成 */
			bool Create(ID3D11Texture2D* texture);
			/** 解放 */
			void Release();
			/** ShaderResourceView取得 */
			inline ID3D11ShaderResourceView*& GetBody() { return shaderResourceView_; }
		};




		/*******************************************/


		/**
		 * UnorderedAccessView
		 * NOTE: コンピュートシェーダーとピクセルシェーダーの出力に使用するビュー
		 */
		class UnorderedAccessView
		{
		private:
			ID3D11UnorderedAccessView* unorderedAccessView_;


		public:
			UnorderedAccessView();
			~UnorderedAccessView();

			/** StructuredBuffer用のUAV生成 */
			bool Create(StructuredBuffer& structuredBuffer);
			/** テクスチャ用のUAV生成 */
			bool Create(ID3D11Texture2D* texture);
			/** 解放 */
			void Release();
			/** UnorderedAccessView取得 */
			inline ID3D11UnorderedAccessView*& GetBody() { return unorderedAccessView_; }
		};




		/*******************************************/


		/**
		 * レンダリングターゲット
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

			/** レンダリングターゲット生成 */
			bool Create(int32_t width, int32_t height, int32_t mipLevel, DXGI_FORMAT colorFormat, DXGI_FORMAT depthStencilFormat, DXGI_SAMPLE_DESC multiSampleDesc, ID3D11Texture2D* renderTarget = nullptr, ID3D11Texture2D* depthStencil = nullptr);
			/** 解放 */
			void Release();

			/** レンダリングターゲット取得 */
			inline ID3D11Texture2D* GetRenderTarget() const
			{
				return renderTarget_;
			}
			/** レンダリングターゲットビュー取得 */
			inline ID3D11RenderTargetView* GetrenderTargetView() const
			{
				return renderTargetView_;
			}
			/** レンダリングターゲットSRV取得 */
			inline ShaderResourceView& GetRenderTargetSRV()
			{
				return renderTargetSRV_;
			}
			/** レンダリングターゲットUAV取得 */
			inline UnorderedAccessView& GetRenderTargetUAV()
			{
				return renderTargetUAV_;
			}
			/** デプスステンシルビュー取得 */
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
			/** デバイスコンテキスト */
			ID3D11DeviceContext* d3dDeviceContext_;
			/** ビューポート */
			D3D11_VIEWPORT viewport_;
			/** 現在使用中のレンダリングターゲットビュー */
			ID3D11RenderTargetView* renderTargetViews_[MAX_MRT_NUM];
			/** 現在設定されているデプスステンシルビュー */
			ID3D11DepthStencilView* depthStencilView_;
			/** レンダリングターゲットビューの数 */
			uint32_t renderTargetViewNum_;


		public:
			RenderContext();
			~RenderContext();

			/** 初期化 */
			void Initialize(ID3D11DeviceContext* d3dDeviceContext);
			/** レンダリングターゲットビュー設定 */
			void OMSetRenderTargets(uint32_t numViews, RenderTarget* renderTarget);
			/** ビューポート設定 */
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
			/** ラスタライザ設定 */
			void RSSetState(ID3D11RasterizerState* state)
			{
				d3dDeviceContext_->RSSetState(state);
			}
			/** レンダリングターゲットクリア */
			void ClearRenderTargetView(uint32_t index, float* clearColor)
			{
				if (renderTargetViews_ && index < renderTargetViewNum_) {
					d3dDeviceContext_->ClearRenderTargetView(renderTargetViews_[index], clearColor);
					d3dDeviceContext_->ClearDepthStencilView(depthStencilView_, D3D11_CLEAR_DEPTH, 1.0f, 0);
				}
			}
			/** 頂点バッファ設定 */
			void IASetVertexBuffer(VertexBuffer& vertexBuffer)
			{
				uint32_t offset = 0;
				uint32_t stride = vertexBuffer.GetStride();
				d3dDeviceContext_->IASetVertexBuffers(0, 1, &vertexBuffer.GetBody(), &stride, &offset);
			}
			/** インデックスバッファ設定 */
			void IASetIndexBuffer(IndexBuffer& indexBuffer)
			{
				d3dDeviceContext_->IASetIndexBuffer(indexBuffer.GetBody(), DXGI_FORMAT_R32_UINT, 0);
			}
			/** プリミティブトポロジー設定 */
			void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology)
			{
				d3dDeviceContext_->IASetPrimitiveTopology(topology);
			}
			/** VSステージに定数バッファ設定 */
			void VSSetConstantBuffer(uint32_t startSlot, ConstantBuffer& constantBuffer)
			{
				d3dDeviceContext_->VSSetConstantBuffers(startSlot, 1, &constantBuffer.GetBody());
			}
			/** PSステージに定数バッファ設定 */
			void PSSetConstantBuffer(uint32_t startSlot, ConstantBuffer& constantBuffer)
			{
				d3dDeviceContext_->PSSetConstantBuffers(startSlot, 1, &constantBuffer.GetBody());
			}
			/** PSステージにSRV設定 */
			void PSSetShaderResource(uint32_t startSlot, ShaderResourceView& shaderResourceView)
			{
				d3dDeviceContext_->PSSetShaderResources(startSlot, 1, &shaderResourceView.GetBody());
			}
			/** PSステージからSRVを外す */
			void PSUnsetShaderResource(uint32_t slot)
			{
				ID3D11ShaderResourceView* view[] = {
					nullptr,
				};
				d3dDeviceContext_->PSSetShaderResources(slot, 1, view);
			}
			/** PSステージにサンプラステート設定 */
			void PsSetSampler(uint32_t startSlot, SamplerState& samplerState)
			{
				d3dDeviceContext_->PSSetSamplers(startSlot, 1, &samplerState.GetBody());
			}
			/** 頂点シェーダー設定 */
			void VSSetShader(Shader& shader)
			{
				d3dDeviceContext_->VSSetShader((ID3D11VertexShader*)shader.GetBody(), nullptr, 0);
			}
			/** ピクセルシェーダー設定 */
			void PSSetShader(Shader& shader)
			{
				d3dDeviceContext_->PSSetShader((ID3D11PixelShader*)shader.GetBody(), nullptr, 0);
			}
			/** コンピュートシェーダー設定 */
			void CSSetShader(Shader& shader)
			{
				d3dDeviceContext_->CSSetShader((ID3D11ComputeShader*)shader.GetBody(), nullptr, 0);
			}
			/** CSステージに定数バッファ設定 */
			void CSSetConstantBuffer(uint32_t startSlot, ConstantBuffer& constantBuffer)
			{
				d3dDeviceContext_->CSSetConstantBuffers(startSlot, 1, &constantBuffer.GetBody());
			}
			/** コンピュートシェーダーにSRV設定 */
			void CSSetShaderResource(uint32_t startSlot, ShaderResourceView& shaderResourceView)
			{
				d3dDeviceContext_->CSSetShaderResources(startSlot, 1, &shaderResourceView.GetBody());
			}
			/** コンピュートシェーダーからSRVを外す */
			void CSUnsetShaderResource(uint32_t slot)
			{
				ID3D11ShaderResourceView* view[] = {
					nullptr,
				};
				d3dDeviceContext_->CSSetShaderResources(slot, 1, view);
			}
			/** コンピュートシェーダーにUAV設定 */
			void CSSetUnorderedAccessView(uint32_t startSlot, UnorderedAccessView& unorderedAccessView)
			{
				d3dDeviceContext_->CSSetUnorderedAccessViews(startSlot, 1, &unorderedAccessView.GetBody(), nullptr);
			}
			/** コンピュートシェーダーからUAVを外す */
			void CSUnsetUnorderedAccessView(uint32_t slot)
			{
				ID3D11UnorderedAccessView* view[] = {
					nullptr,
				};
				d3dDeviceContext_->CSSetUnorderedAccessViews(slot, 1, view, nullptr);
			}
			/** 描画 */
			void Draw(uint32_t vertexCount, uint32_t startVertexLocation)
			{
				d3dDeviceContext_->Draw(vertexCount, startVertexLocation);
			}
			void DrawIndexed(uint32_t indexCount)
			{
				d3dDeviceContext_->DrawIndexed(indexCount, 0, 0);
			}
			/** ディスパッチ */
			void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				d3dDeviceContext_->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			}
			/** 入力レイアウト設定 */
			void IASetInputLayout(ID3D11InputLayout* inputLayout)
			{
				d3dDeviceContext_->IASetInputLayout(inputLayout);
			}
			/** リソースコピー */
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
			/** マップ */
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
			/** サブリソース更新 */
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