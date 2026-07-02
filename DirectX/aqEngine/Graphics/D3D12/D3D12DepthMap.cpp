#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12DepthMap.h"


namespace aq
{
	namespace graphics
	{
		bool D3D12DepthMap::Create(ID3D12Device* device, uint32_t resolution,
		                           const D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandles,
		                           D3D12_CPU_DESCRIPTOR_HANDLE srvArrayStaging,
		                           const D3D12_CPU_DESCRIPTOR_HANDLE* srvSliceStaging)
		{
			Release();
			if (!device) return false;
			resolution_ = resolution;

			// R32_TYPELESS の Texture2DArray (DSV=D32_FLOAT, SRV=R32_FLOAT で別ビュー)
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width            = resolution;
			rd.Height           = resolution;
			rd.DepthOrArraySize = static_cast<UINT16>(kArraySize);
			rd.MipLevels        = 1;
			rd.Format           = DXGI_FORMAT_R32_TYPELESS;
			rd.SampleDesc.Count = 1;
			rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE clearDepth = {};
			clearDepth.Format             = DXGI_FORMAT_D32_FLOAT;
			clearDepth.DepthStencil.Depth = 1.0f;

			D3D12_HEAP_PROPERTIES defaultHeap = {};
			defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

			HRESULT hr = device->CreateCommittedResource(
				&defaultHeap, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearDepth, IID_PPV_ARGS(&texture_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 シャドウ深度テクスチャ作成失敗"); return false; }
			state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

			// スライス別 DSV
			for (uint32_t i = 0; i < kArraySize; ++i)
			{
				D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
				dsv.Format                         = DXGI_FORMAT_D32_FLOAT;
				dsv.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				dsv.Texture2DArray.MipSlice        = 0;
				dsv.Texture2DArray.FirstArraySlice = i;
				dsv.Texture2DArray.ArraySize       = 1;
				device->CreateDepthStencilView(texture_, &dsv, dsvHandles[i]);
				dsvHandles_[i] = dsvHandles[i];
			}

			// 全スライス配列 SRV (R32_FLOAT)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
				srv.Format                          = DXGI_FORMAT_R32_FLOAT;
				srv.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				srv.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.Texture2DArray.MipLevels        = 1;
				srv.Texture2DArray.FirstArraySlice  = 0;
				srv.Texture2DArray.ArraySize        = kArraySize;
				device->CreateShaderResourceView(texture_, &srv, srvArrayStaging);
				srv_.SetStagingCPUHandle(srvArrayStaging);
				srv_.SetBarrierSource(texture_, &state_);  // SRV読み取り前に DEPTH_WRITE→SRV 遷移
			}

			// スライス別 SRV (デバッグ表示用)
			for (uint32_t i = 0; i < kArraySize; ++i)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
				srv.Format                          = DXGI_FORMAT_R32_FLOAT;
				srv.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				srv.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.Texture2DArray.MipLevels        = 1;
				srv.Texture2DArray.FirstArraySlice  = i;
				srv.Texture2DArray.ArraySize        = 1;
				device->CreateShaderResourceView(texture_, &srv, srvSliceStaging[i]);
				sliceSrvs_[i].SetStagingCPUHandle(srvSliceStaging[i]);
				sliceSrvs_[i].SetBarrierSource(texture_, &state_);
			}
			return true;
		}


		void D3D12DepthMap::Release()
		{
			SafeReleaseD3D12(texture_);
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D12
