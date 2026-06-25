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
			// ArraySize = kArraySize: ディレクショナルライト 1 本につき 1 スライス
			D3D11_TEXTURE2D_DESC texDesc = {};
			texDesc.Width          = resolution;
			texDesc.Height         = resolution;
			texDesc.MipLevels      = 1;
			texDesc.ArraySize      = kArraySize;
			texDesc.Format         = DXGI_FORMAT_R32_TYPELESS;
			texDesc.SampleDesc     = { 1, 0 };
			texDesc.Usage          = D3D11_USAGE_DEFAULT;
			texDesc.BindFlags      = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

			HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &texture_);
			if (FAILED(hr)) { EngineAssert(false); return false; }

			// スライスごとに独立した DSV を生成
			for (uint32_t i = 0; i < kArraySize; ++i)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
				dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
				dsvDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				dsvDesc.Texture2DArray.MipSlice        = 0;
				dsvDesc.Texture2DArray.FirstArraySlice = i;
				dsvDesc.Texture2DArray.ArraySize       = 1;
				hr = device->CreateDepthStencilView(texture_, &dsvDesc, &dsvs_[i]);
				if (FAILED(hr)) { EngineAssert(false); return false; }
			}

			// SRV: 全スライスを Texture2DArray として参照 (シャドウサンプリング用)
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
			srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip  = 0;
			srvDesc.Texture2DArray.MipLevels        = 1;
			srvDesc.Texture2DArray.FirstArraySlice  = 0;
			srvDesc.Texture2DArray.ArraySize        = kArraySize;
			hr = device->CreateShaderResourceView(texture_, &srvDesc, &srv_.GetBody());
			if (FAILED(hr)) { EngineAssert(false); return false; }

			// SRV: スライスごとの単体 SRV (ImGui デバッグ表示用)
			for (uint32_t i = 0; i < kArraySize; ++i)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC sliceSrvDesc = {};
				sliceSrvDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
				sliceSrvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				sliceSrvDesc.Texture2DArray.MostDetailedMip  = 0;
				sliceSrvDesc.Texture2DArray.MipLevels        = 1;
				sliceSrvDesc.Texture2DArray.FirstArraySlice  = i;
				sliceSrvDesc.Texture2DArray.ArraySize        = 1;
				hr = device->CreateShaderResourceView(texture_, &sliceSrvDesc, &sliceSrvs_[i].GetBody());
				if (FAILED(hr)) { EngineAssert(false); return false; }
			}

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
			for (uint32_t i = 0; i < kArraySize; ++i)
				sliceSrvs_[i].Release();
			srv_.Release();
			for (uint32_t i = 0; i < kArraySize; ++i)
			{
				if (dsvs_[i]) { dsvs_[i]->Release(); dsvs_[i] = nullptr; }
			}
			if (texture_) { texture_->Release(); texture_ = nullptr; }
			resolution_ = 0;
		}
	}
}
