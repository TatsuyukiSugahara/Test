#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12GpuBuffer.h"


namespace aq
{
	namespace graphics
	{
		bool D3D12GpuBuffer::Create(ID3D12Device* device, uint32_t byteSize, uint32_t structuredStride,
		                            bool srv, bool uav, const void* initData,
		                            D3D12_CPU_DESCRIPTOR_HANDLE srvStaging,
		                            D3D12_CPU_DESCRIPTOR_HANDLE uavStaging)
		{
			Release();
			if (!device || byteSize == 0) return false;
			byteSize_  = byteSize;
			isDefault_ = uav;  // UAV を持つものは DEFAULT ヒープ

			D3D12_HEAP_PROPERTIES heap = {};
			heap.Type = uav ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width            = byteSize;
			desc.Height           = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels        = 1;
			desc.Format           = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count = 1;
			desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags            = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

			// バッファは常に COMMON で生成される (UAV でも InitialState は無視される)。
			// COMMON のバッファは初回使用時に UAV/INDEX/INDIRECT へ暗黙昇格でき、明示遷移も可能。
			const D3D12_RESOURCE_STATES initState = uav
				? D3D12_RESOURCE_STATE_COMMON
				: D3D12_RESOURCE_STATE_GENERIC_READ;         // UPLOAD は常時 GENERIC_READ

			HRESULT hr = device->CreateCommittedResource(
				&heap, D3D12_HEAP_FLAG_NONE, &desc, initState, nullptr, IID_PPV_ARGS(&resource_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12GpuBuffer リソース作成失敗"); return false; }
			state_ = initState;

			// UPLOAD ヒープなら永続 Map して初期データを書く
			if (!uav)
			{
				D3D12_RANGE readRange = { 0, 0 };
				void* m = nullptr;
				if (SUCCEEDED(resource_->Map(0, &readRange, &m)) && m)
				{
					mapped_ = static_cast<uint8_t*>(m);
					if (initData) std::memcpy(mapped_, initData, byteSize);
				}
			}

			// SRV
			if (srv)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
				sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				sd.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
				if (structuredStride > 0)
				{
					sd.Format                     = DXGI_FORMAT_UNKNOWN;
					sd.Buffer.NumElements         = byteSize / structuredStride;
					sd.Buffer.StructureByteStride = structuredStride;
					sd.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
				}
				else
				{
					sd.Format                     = DXGI_FORMAT_R32_TYPELESS;
					sd.Buffer.NumElements         = byteSize / 4u;
					sd.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
				}
				device->CreateShaderResourceView(resource_, &sd, srvStaging);
				srv_.SetStagingCPUHandle(srvStaging);
				// DEFAULT ヒープのみ遷移対象 (UPLOAD は常時 GENERIC_READ で遷移不可)
				if (isDefault_) srv_.SetBarrierSource(resource_, &state_);
				srvValid_ = true;
			}

			// UAV (RAW)
			if (uav)
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
				ud.Format              = DXGI_FORMAT_R32_TYPELESS;
				ud.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
				ud.Buffer.NumElements  = byteSize / 4u;
				ud.Buffer.Flags        = D3D12_BUFFER_UAV_FLAG_RAW;
				device->CreateUnorderedAccessView(resource_, nullptr, &ud, uavStaging);
				uav_.SetStagingCPUHandle(uavStaging);
				uav_.SetBarrierSource(resource_, &state_);
				uavValid_ = true;
			}

			return true;
		}


		void D3D12GpuBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			byteSize_ = 0;
			srvValid_ = false;
			uavValid_ = false;
			isDefault_ = false;
			state_ = D3D12_RESOURCE_STATE_COMMON;
		}


		D3D12_INDEX_BUFFER_VIEW D3D12GpuBuffer::GetIndexBufferView() const
		{
			D3D12_INDEX_BUFFER_VIEW v = {};
			if (resource_)
			{
				v.BufferLocation = resource_->GetGPUVirtualAddress();
				v.SizeInBytes    = byteSize_;
				v.Format         = DXGI_FORMAT_R32_UINT;
			}
			return v;
		}


		void D3D12GpuBuffer::Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES after)
		{
			if (!list || !resource_ || !isDefault_) return;  // UPLOAD は遷移不可
			if (state_ == after) return;
			D3D12_RESOURCE_BARRIER b = {};
			b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource   = resource_;
			b.Transition.StateBefore = state_;
			b.Transition.StateAfter  = after;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			list->ResourceBarrier(1, &b);
			state_ = after;
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D12
