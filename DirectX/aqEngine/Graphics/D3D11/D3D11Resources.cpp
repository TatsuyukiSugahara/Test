#include "aq.h"
#include "D3D11RenderResources.h"
#include "D3D11Buffers.h"
#include "D3D11GraphicsDeviceImpl.h"



namespace
{
	using namespace aq::graphics;

	D3D11_FILTER ToD3D11Filter(FilterMode f)
	{
		switch (f) {
			case FilterMode::MinMagMipPoint:       return D3D11_FILTER_MIN_MAG_MIP_POINT;
			case FilterMode::MinMagMipLinear:      return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			case FilterMode::MinMagLinearMipPoint: return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			case FilterMode::Anisotropic:          return D3D11_FILTER_ANISOTROPIC;
			default:                               return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		}
	}

	D3D11_TEXTURE_ADDRESS_MODE ToD3D11Address(AddressMode a)
	{
		switch (a) {
			case AddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
			case AddressMode::Wrap:   return D3D11_TEXTURE_ADDRESS_WRAP;
			case AddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
			case AddressMode::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
			default:                  return D3D11_TEXTURE_ADDRESS_CLAMP;
		}
	}

	DXGI_FORMAT ToD3D11Format(PixelFormat p)
	{
		switch (p) {
			case PixelFormat::R8G8B8A8_Unorm:      return DXGI_FORMAT_R8G8B8A8_UNORM;
			case PixelFormat::R8G8B8A8_Unorm_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			case PixelFormat::B8G8R8A8_Unorm:      return DXGI_FORMAT_B8G8R8A8_UNORM;
			case PixelFormat::B8G8R8A8_Unorm_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			case PixelFormat::D24_Unorm_S8_Uint:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
			case PixelFormat::R16G16B16A16_Float:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case PixelFormat::R32_Float:            return DXGI_FORMAT_R32_FLOAT;
			case PixelFormat::R32G32B32A32_Float:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
			case PixelFormat::BC1_Unorm:            return DXGI_FORMAT_BC1_UNORM;
			case PixelFormat::BC1_Unorm_SRGB:       return DXGI_FORMAT_BC1_UNORM_SRGB;
			case PixelFormat::BC2_Unorm:            return DXGI_FORMAT_BC2_UNORM;
			case PixelFormat::BC2_Unorm_SRGB:       return DXGI_FORMAT_BC2_UNORM_SRGB;
			case PixelFormat::BC3_Unorm:            return DXGI_FORMAT_BC3_UNORM;
			case PixelFormat::BC3_Unorm_SRGB:       return DXGI_FORMAT_BC3_UNORM_SRGB;
			case PixelFormat::BC4_Unorm:            return DXGI_FORMAT_BC4_UNORM;
			case PixelFormat::BC5_Unorm:            return DXGI_FORMAT_BC5_UNORM;
			case PixelFormat::BC6H_UFloat16:        return DXGI_FORMAT_BC6H_UF16;
			case PixelFormat::BC7_Unorm:            return DXGI_FORMAT_BC7_UNORM;
			case PixelFormat::BC7_Unorm_SRGB:       return DXGI_FORMAT_BC7_UNORM_SRGB;
			default:                                return DXGI_FORMAT_UNKNOWN;
		}
	}
}


namespace aq
{
	namespace graphics
	{
		bool SamplerState::Create(const SamplerDesc& desc)
		{
			Release();
			D3D11_SAMPLER_DESC d3dDesc = {};
			if (desc.isComparison) {
				// シャドウマップ用: 線形フィルタ + LESS_EQUAL 比較 + 境界=白 (範囲外は全て明るい)
				d3dDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
				d3dDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
				d3dDesc.BorderColor[0] = d3dDesc.BorderColor[1] =
				d3dDesc.BorderColor[2] = d3dDesc.BorderColor[3] = 1.0f;
			} else {
				d3dDesc.Filter         = ToD3D11Filter(desc.filter);
				d3dDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			}
			d3dDesc.AddressU      = ToD3D11Address(desc.addressU);
			d3dDesc.AddressV      = ToD3D11Address(desc.addressV);
			d3dDesc.AddressW      = ToD3D11Address(desc.addressW);
			d3dDesc.MipLODBias    = desc.mipLODBias;
			d3dDesc.MaxAnisotropy = desc.maxAniso;
			d3dDesc.MinLOD        = desc.minLOD;
			d3dDesc.MaxLOD        = desc.maxLOD;
			HRESULT hr = D3D11GraphicsDeviceImpl::GetStaticDevice()->CreateSamplerState(&d3dDesc, &samplerState_);
			if (FAILED(hr)) { EngineAssert(false); return false; }
			return true;
		}


		/*******************************************/


		bool ShaderResourceView::Create(StructuredBuffer& structuredBuffer)
		{
			Release();
			ID3D11Buffer* buffer = structuredBuffer.GetBody();
			if (!buffer) return true;

			D3D11_BUFFER_DESC descBuffer = {};
			buffer->GetDesc(&descBuffer);

			D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension        = D3D11_SRV_DIMENSION_BUFFEREX;
			desc.BufferEx.FirstElement = 0;
			desc.Format               = DXGI_FORMAT_UNKNOWN;
			desc.BufferEx.NumElements  = descBuffer.ByteWidth / descBuffer.StructureByteStride;

			HRESULT hr = D3D11GraphicsDeviceImpl::GetStaticDevice()->CreateShaderResourceView(buffer, &desc, &shaderResourceView_);
			return SUCCEEDED(hr);
		}


		bool ShaderResourceView::Create(ID3D11Texture2D* texture)
		{
			Release();
			if (!texture) return true;

			D3D11_TEXTURE2D_DESC textureDesc = {};
			texture->GetDesc(&textureDesc);

			D3D11_SHADER_RESOURCE_VIEW_DESC descSRV = {};
			descSRV.Format = textureDesc.Format;
			if (textureDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) {
				descSRV.ViewDimension              = D3D11_SRV_DIMENSION_TEXTURECUBE;
				descSRV.TextureCube.MostDetailedMip = 0;
				descSRV.TextureCube.MipLevels      = textureDesc.MipLevels;
			}
			else if (textureDesc.ArraySize > 1) {
				descSRV.ViewDimension                  = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				descSRV.Texture2DArray.MostDetailedMip = 0;
				descSRV.Texture2DArray.MipLevels       = textureDesc.MipLevels;
				descSRV.Texture2DArray.FirstArraySlice = 0;
				descSRV.Texture2DArray.ArraySize       = textureDesc.ArraySize;
			}
			else {
				descSRV.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
				descSRV.Texture2D.MipLevels = textureDesc.MipLevels;
			}

			HRESULT hr = D3D11GraphicsDeviceImpl::GetStaticDevice()->CreateShaderResourceView(texture, &descSRV, &shaderResourceView_);
			return SUCCEEDED(hr);
		}


		/*******************************************/


		bool UnorderedAccessView::Create(StructuredBuffer& structuredBuffer)
		{
			Release();
			ID3D11Buffer* buffer = structuredBuffer.GetBody();
			if (!buffer) return true;

			D3D11_BUFFER_DESC descBuffer = {};
			buffer->GetDesc(&descBuffer);

			D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
			desc.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
			desc.Buffer.FirstElement = 0;
			desc.Format              = DXGI_FORMAT_UNKNOWN;
			desc.Buffer.NumElements  = descBuffer.ByteWidth / descBuffer.StructureByteStride;

			HRESULT hr = D3D11GraphicsDeviceImpl::GetStaticDevice()->CreateUnorderedAccessView(buffer, &desc, &unorderedAccessView_);
			return SUCCEEDED(hr);
		}


		bool UnorderedAccessView::Create(ID3D11Texture2D* texture)
		{
			Release();
			if (!texture) return true;

			D3D11_TEXTURE2D_DESC textureDesc = {};
			texture->GetDesc(&textureDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC descUAV = {};
			descUAV.Buffer.FirstElement = 0;
			descUAV.Buffer.NumElements  = textureDesc.Width / textureDesc.Height;
			descUAV.Buffer.Flags        = 0;
			descUAV.Format              = textureDesc.Format;
			descUAV.ViewDimension       = D3D11_UAV_DIMENSION_TEXTURE2D;

			HRESULT hr = D3D11GraphicsDeviceImpl::GetStaticDevice()->CreateUnorderedAccessView(texture, &descUAV, &unorderedAccessView_);
			return SUCCEEDED(hr);
		}


		/*******************************************/


		bool RenderTarget::Create(int32_t width, int32_t height, int32_t mipLevel,
			PixelFormat colorFormat, PixelFormat depthStencilFormat,
			SampleDesc multiSampleDesc,
			ID3D11Texture2D* renderTarget, ID3D11Texture2D* depthStencil)
		{
			Release();
			ID3D11Device* device = D3D11GraphicsDeviceImpl::GetStaticDevice();

			DXGI_SAMPLE_DESC sampleDesc = { multiSampleDesc.count, multiSampleDesc.quality };

			D3D11_TEXTURE2D_DESC textureDesc = {};
			textureDesc.Width      = width;
			textureDesc.Height     = height;
			textureDesc.MipLevels  = mipLevel;
			textureDesc.ArraySize  = 1;
			textureDesc.Format     = ToD3D11Format(colorFormat);
			textureDesc.SampleDesc = sampleDesc;
			textureDesc.Usage      = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			HRESULT hr;
			if (renderTarget == nullptr) {
				hr = device->CreateTexture2D(&textureDesc, nullptr, &renderTarget_);
				if (FAILED(hr)) { EngineAssert(false); return false; }
			} else {
				renderTarget_ = renderTarget;
				renderTarget_->AddRef();
			}

			hr = device->CreateRenderTargetView(renderTarget_, nullptr, &renderTargetView_);
			if (FAILED(hr)) { EngineAssert(false); return false; }

			renderTargetSRV_.Create(renderTarget_);
			renderTargetUAV_.Create(renderTarget_);

			if (depthStencilFormat != PixelFormat::Unknown) {
				textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				textureDesc.Format    = ToD3D11Format(depthStencilFormat);
				if (depthStencil == nullptr) {
					hr = device->CreateTexture2D(&textureDesc, nullptr, &depthStencil_);
					if (FAILED(hr)) { EngineAssert(false); return false; }
				} else {
					depthStencil_ = depthStencil;
					depthStencil_->AddRef();
				}

				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
				dsvDesc.Format             = textureDesc.Format;
				dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Texture2D.MipSlice = 0;
				hr = device->CreateDepthStencilView(depthStencil_, &dsvDesc, &depthStencilView_);
				if (FAILED(hr)) { EngineAssert(false); return false; }
			}
			return true;
		}


		/*******************************************/


		std::unique_ptr<IShaderResourceView> D3D11GraphicsDeviceImpl::CreateTexture2D(const Texture2DDesc& texDesc, const ImageData& imgData)
		{
			D3D11_TEXTURE2D_DESC d3dDesc = {};
			d3dDesc.Width              = texDesc.width;
			d3dDesc.Height             = texDesc.height;
			d3dDesc.MipLevels          = texDesc.mipLevels;
			d3dDesc.ArraySize          = texDesc.arraySize;
			d3dDesc.Format             = ToD3D11Format(texDesc.format);
			d3dDesc.SampleDesc.Count   = 1;
			d3dDesc.SampleDesc.Quality = 0;
			d3dDesc.Usage              = D3D11_USAGE_DEFAULT;
			d3dDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
			d3dDesc.MiscFlags          = texDesc.isCubemap ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

			D3D11_SUBRESOURCE_DATA singleInitData = {};
			std::vector<D3D11_SUBRESOURCE_DATA> initDataArray;
			const D3D11_SUBRESOURCE_DATA* initData = nullptr;
			if (imgData.subresources && imgData.subresourceCount > 0) {
				initDataArray.reserve(imgData.subresourceCount);
				for (uint32_t index = 0; index < imgData.subresourceCount; ++index) {
					const ImageSubresourceData& src = imgData.subresources[index];
					D3D11_SUBRESOURCE_DATA dst = {};
					dst.pSysMem          = src.pixels;
					dst.SysMemPitch      = src.rowPitch;
					dst.SysMemSlicePitch = src.slicePitch;
					initDataArray.push_back(dst);
				}
				initData = initDataArray.data();
			}
			else {
				singleInitData.pSysMem          = imgData.pixels;
				singleInitData.SysMemPitch      = imgData.rowPitch;
				singleInitData.SysMemSlicePitch = imgData.slicePitch;
				initData = &singleInitData;
			}

			ID3D11Texture2D* tex = nullptr;
			HRESULT hr = GetStaticDevice()->CreateTexture2D(&d3dDesc, initData, &tex);
			if (FAILED(hr)) return nullptr;

			auto srv = std::make_unique<ShaderResourceView>();
			srv->Create(tex);
			tex->Release();
			return srv;
		}
	}
}
