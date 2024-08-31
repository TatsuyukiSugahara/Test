#include "../EnginePreCompile.h"
#include "RenderContext.h"
#include "../Engine.h"

namespace engine
{
	namespace graphics
	{
		SamplerState::SamplerState()
			: samplerState_(nullptr)
		{
		}


		SamplerState::~SamplerState()
		{
			Release();
		}


		bool SamplerState::Create(const D3D11_SAMPLER_DESC& desc)
		{
			Release();

			HRESULT hr = Engine::Get().GetD3DDevice()->CreateSamplerState(&desc, &samplerState_);
			if (FAILED(hr)) {
				EngineAssert(false);
				return false;
			}
			return true;
		}


		void SamplerState::Release()
		{
			if (samplerState_) {
				samplerState_->Release();
				samplerState_ = nullptr;
			}
		}




		/*******************************************/


		ShaderResourceView::ShaderResourceView()
		{
		}


		ShaderResourceView::~ShaderResourceView()
		{
			Release();
		}


		bool ShaderResourceView::Create(StructuredBuffer& structuredBuffer)
		{
			Release();
			ID3D11Buffer* buffer = structuredBuffer.GetBody();
			if (buffer) {
				D3D11_BUFFER_DESC descBuffer;
				ZeroMemory(&descBuffer, sizeof(descBuffer));
				buffer->GetDesc(&descBuffer);

				D3D11_SHADER_RESOURCE_VIEW_DESC desc;
				ZeroMemory(&desc, sizeof(desc));
				desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
				desc.BufferEx.FirstElement = 0;
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.BufferEx.NumElements = descBuffer.ByteWidth / descBuffer.StructureByteStride;

				HRESULT hr = Engine::Get().GetD3DDevice()->CreateShaderResourceView(buffer, &desc, &shaderResourceView_);
				if (FAILED(hr)) {
					return false;
				}
			}
			return true;
		}


		bool ShaderResourceView::Create(ID3D11Texture2D* texture)
		{
			Release();
			if (texture) {
				D3D11_TEXTURE2D_DESC textureDesc;
				texture->GetDesc(&textureDesc);
				D3D11_SHADER_RESOURCE_VIEW_DESC descSRV;
				ZeroMemory(&descSRV, sizeof(descSRV));
				descSRV.Format = textureDesc.Format;
				descSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				descSRV.Texture2D.MipLevels = textureDesc.MipLevels;

				HRESULT hr = Engine::Get().GetD3DDevice()->CreateShaderResourceView(texture, &descSRV, &shaderResourceView_);
				if (FAILED(hr)) {
					return false;
				}
			}
			return true;
		}


		void ShaderResourceView::Release()
		{
			if (shaderResourceView_) {
				shaderResourceView_->Release();
				shaderResourceView_ = nullptr;
			}
		}




		/*******************************************/


		UnorderedAccessView::UnorderedAccessView()
		{
		}


		UnorderedAccessView::~UnorderedAccessView()
		{
		}


		bool UnorderedAccessView::Create(StructuredBuffer& structuredBuffer)
		{
			Release();
			ID3D11Buffer* buffer = structuredBuffer.GetBody();
			if (buffer) {
				D3D11_BUFFER_DESC descBuffer;
				ZeroMemory(&descBuffer, sizeof(descBuffer));
				buffer->GetDesc(&descBuffer);

				D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
				ZeroMemory(&desc, sizeof(desc));
				desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				desc.Buffer.FirstElement = 0;

				HRESULT hr = Engine::Get().GetD3DDevice()->CreateUnorderedAccessView(buffer, &desc, &unorderedAccessView_);
				if (FAILED(hr)) {
					return false;
				}
			}
			return true;
		}


		bool UnorderedAccessView::Create(ID3D11Texture2D* texture)
		{
			Release();
			if (texture) {
				D3D11_TEXTURE2D_DESC textureDesc;
				texture->GetDesc(&textureDesc);
				D3D11_UNORDERED_ACCESS_VIEW_DESC descUAV;
				ZeroMemory(&descUAV, sizeof(descUAV));
				descUAV.Buffer.FirstElement = 0;
				descUAV.Buffer.NumElements = textureDesc.Width / textureDesc.Height;
				descUAV.Buffer.Flags = 0;
				descUAV.Format = textureDesc.Format;
				descUAV.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

				HRESULT hr = Engine::Get().GetD3DDevice()->CreateUnorderedAccessView(texture, &descUAV, &unorderedAccessView_);
				if (FAILED(hr)) {
					return false;
				}
			}
			return true;
		}


		void UnorderedAccessView::Release()
		{
			if (unorderedAccessView_) {
				unorderedAccessView_->Release();
				unorderedAccessView_ = nullptr;
			}
		}




		/*******************************************/


		RenderTarget::RenderTarget()
		{
		}


		RenderTarget::~RenderTarget()
		{
			Release();
		}


		bool RenderTarget::Create(int32_t width, int32_t height, int32_t mipLevel, DXGI_FORMAT colorFormat, DXGI_FORMAT depthStencilFormat, DXGI_SAMPLE_DESC multiSampleDesc, ID3D11Texture2D* renderTarget, ID3D11Texture2D* depthStencil)
		{
			Release();

			// レンダリングターゲット生成
			D3D11_TEXTURE2D_DESC textureDesc;
			memory::Clear(&textureDesc, sizeof(textureDesc));
			textureDesc.Width = width;
			textureDesc.Height = height;
			textureDesc.MipLevels = mipLevel;
			textureDesc.ArraySize = 1;
			textureDesc.Format = colorFormat;
			textureDesc.SampleDesc = multiSampleDesc;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;
			ID3D11Device* d3dDevice = Engine::Get().GetD3DDevice();
			HRESULT hr;
			if (renderTarget == nullptr) {
				hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &renderTarget_);
				if (FAILED(hr)) {
					// レンダリングターゲット生成失敗
					EngineAssert(false);
					return false;
				}
			} else {
				// レンダリングターゲットが指定されているため、参照カウンタを加算する
				renderTarget_ = renderTarget;
				renderTarget_->AddRef();
			}
			// レンダリングターゲットビュー生成
			hr = d3dDevice->CreateRenderTargetView(renderTarget_, nullptr, &renderTargetView_);
			if (FAILED(hr)) {
				EngineAssert(false);
				return false;
			}
			// レンダリングターゲットビューSRV生成
			renderTargetSRV_.Create(renderTarget_);
			// レンダリングターゲットビューUAV生成
			renderTargetUAV_.Create(renderTarget_);
			// デプスステンシル生成
			if (depthStencilFormat != DXGI_FORMAT_UNKNOWN) {
				textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				textureDesc.Format = depthStencilFormat;
				if (depthStencil == nullptr) {
					hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &depthStencil_);
					if (FAILED(hr)) {
						EngineAssert(false);
						return false;
					}
				} else {
					// デプスステンシルが指定されているため、参照カウンタを加算する
					depthStencil_ = depthStencil;
					depthStencil_->AddRef();
				}
				// デプスステンシルビュー生成
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				memory::Clear(&dsvDesc, sizeof(dsvDesc));
				dsvDesc.Format = textureDesc.Format;
				dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Texture2D.MipSlice = 0;
				hr = d3dDevice->CreateDepthStencilView(depthStencil_, &dsvDesc, &depthStencilView_);
				if (FAILED(hr)) {
					EngineAssert(false);
					return false;
				}
			}
			return true;
		}


		void RenderTarget::Release()
		{
			renderTargetSRV_.Release();
			renderTargetUAV_.Release();
			if (renderTarget_) {
				renderTarget_->Release();
				renderTarget_ = nullptr;
			}
			if (renderTargetView_) {
				renderTargetView_->Release();
				renderTargetView_ = nullptr;
			}
			if (depthStencil_) {
				depthStencil_->Release();
				depthStencil_ = nullptr;
			}
			if (depthStencilView_) {
				depthStencilView_->Release();
				depthStencilView_ = nullptr;
			}
		}




		/*******************************************/


		RenderContext::RenderContext()
		{
		}


		RenderContext::~RenderContext()
		{
		}


		void RenderContext::Initialize(ID3D11DeviceContext* d3dDeviceContext)
		{
			EngineAssertMsg(d3dDeviceContext, "d3dDeviceContextを生成してください\n");
			d3dDeviceContext_ = d3dDeviceContext;
		}
		

		void RenderContext::OMSetRenderTargets(uint32_t numViews, RenderTarget* renderTarget)
		{
			EngineAssertMsg(numViews < MAX_MRT_NUM, "レンダリングターゲット最大数を超えています\n");

			memory::Clear(renderTargetViews_, sizeof(renderTargetViews_));
			depthStencilView_ = nullptr;
			if (renderTarget) {
				depthStencilView_ = renderTarget[0].GetDepthStencilView();
				for (uint32_t i = 0; i < numViews; ++i) {
					renderTargetViews_[i] = renderTarget[i].GetrenderTargetView();
				}
			}
			d3dDeviceContext_->OMSetRenderTargets(numViews, renderTargetViews_, depthStencilView_);
			renderTargetViewNum_ = numViews;
		}




		/*******************************************/


		graphics::ShaderResourceView* Texture::Create2D(const DirectX::TexMetadata& metaData, const DirectX::Image* images)
		{
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = static_cast<uint32_t>(metaData.width);
			desc.Height = static_cast<uint32_t>(metaData.height);
			desc.MipLevels = 1;//static_cast<uint32_t>(metaData.mipLevels);
			desc.ArraySize = static_cast<uint32_t>(metaData.arraySize);
			desc.Format = metaData.format;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			if (metaData.IsCubemap()) {
				desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
			} else {
				desc.MiscFlags = 0;
			}

			const size_t index = metaData.ComputeIndex(0, 0, 0);
			const DirectX::Image& img = images[index];

			std::unique_ptr<D3D11_SUBRESOURCE_DATA[]> initData(new (std::nothrow) D3D11_SUBRESOURCE_DATA[metaData.arraySize]);
			initData[0].pSysMem = img.pixels;
			initData[0].SysMemPitch = static_cast<DWORD>(img.rowPitch);
			initData[0].SysMemSlicePitch = static_cast<DWORD>(img.rowPitch);
			
			ID3D11Texture2D* ppResource = nullptr;
			HRESULT hr = Engine::Get().GetD3DDevice()->CreateTexture2D(&desc, initData.get(), &ppResource);
			if (FAILED(hr)) {
				return nullptr;
			}

			ShaderResourceView* shaderResourceView = new ShaderResourceView();
			shaderResourceView->Create(ppResource);

			return shaderResourceView;
		}
	}
}