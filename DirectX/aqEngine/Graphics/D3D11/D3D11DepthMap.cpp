#include "aq.h"
#include "D3D11DepthMap.h"
#include "D3D11GraphicsDeviceImpl.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace graphics
	{
		D3D11DepthMap::D3D11DepthMap()  = default;
		D3D11DepthMap::~D3D11DepthMap() { Release(); }


		bool D3D11DepthMap::Create(uint32_t resolution)
		{
			Release();
			ID3D11Device* device = D3D11GraphicsDeviceImpl::GetStaticDevice();
			resolution_ = resolution;

			// R32_TYPELESS: DSV と SRV の両方に使用可能
			D3D11_TEXTURE2D_DESC texDesc = {};
			texDesc.Width          = resolution;
			texDesc.Height         = resolution;
			texDesc.MipLevels      = 1;
			texDesc.ArraySize      = 1;
			texDesc.Format         = DXGI_FORMAT_R32_TYPELESS;
			texDesc.SampleDesc     = { 1, 0 };
			texDesc.Usage          = D3D11_USAGE_DEFAULT;
			texDesc.BindFlags      = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

			HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &texture_);
			if (FAILED(hr)) { EngineAssert(false); return false; }

			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
			dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
			dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Texture2D.MipSlice = 0;
			hr = device->CreateDepthStencilView(texture_, &dsvDesc, &dsv_);
			if (FAILED(hr)) { EngineAssert(false); return false; }

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
			srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip  = 0;
			srvDesc.Texture2DArray.MipLevels        = 1;
			srvDesc.Texture2DArray.FirstArraySlice  = 0;
			srvDesc.Texture2DArray.ArraySize        = 1;
			hr = device->CreateShaderResourceView(texture_, &srvDesc, &srv_.GetBody());
			if (FAILED(hr)) { EngineAssert(false); return false; }

			// 比較サンプラー: LESS_EQUAL, 範囲外は 1.0 (明るい) に
			SamplerDesc sampDesc;
			sampDesc.filter       = FilterMode::MinMagMipLinear;
			sampDesc.addressU     = AddressMode::Border;
			sampDesc.addressV     = AddressMode::Border;
			sampDesc.addressW     = AddressMode::Border;
			sampDesc.isComparison = true;
			if (!sampler_.Create(sampDesc)) { EngineAssert(false); return false; }

			return true;
		}


		void D3D11DepthMap::Release()
		{
			sampler_.Release();
			srv_.Release();
			if (dsv_)     { dsv_->Release();     dsv_     = nullptr; }
			if (texture_) { texture_->Release(); texture_ = nullptr; }
			resolution_ = 0;
		}
	}
}
