#include "aq.h"
#include "D3D12Common.h"
#include "D3D12Buffers.h"
#include "D3D12GraphicsDeviceImpl.h"


namespace aq
{
	namespace graphics
	{
		namespace
		{
			// アップロードヒープ上にバッファリソースを作成し、永続 Map する。
			// 成功時 outResource / outMapped を埋めて true を返す。
			bool CreateUploadBuffer(uint32_t byteSize, ID3D12Resource*& outResource, void*& outMapped)
			{
				ID3D12Device* device = D3D12GraphicsDeviceImpl::GetStaticDevice();
				if (!device || byteSize == 0) return false;

				D3D12_HEAP_PROPERTIES heapProps = {};
				heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
				heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

				D3D12_RESOURCE_DESC desc = {};
				desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
				desc.Width            = byteSize;
				desc.Height           = 1;
				desc.DepthOrArraySize = 1;
				desc.MipLevels        = 1;
				desc.Format           = DXGI_FORMAT_UNKNOWN;
				desc.SampleDesc.Count = 1;
				desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				desc.Flags            = D3D12_RESOURCE_FLAG_NONE;

				HRESULT hr = device->CreateCommittedResource(
					&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
					D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outResource));
				if (FAILED(hr)) return false;

				D3D12_RANGE readRange = { 0, 0 };  // CPU は読まない
				hr = outResource->Map(0, &readRange, &outMapped);
				if (FAILED(hr))
				{
					SafeReleaseD3D12(outResource);
					return false;
				}
				return true;
			}

			uint32_t IndexFormatStride(IndexFormat format)
			{
				return format == IndexFormat::UInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
			}
		}


		// ── D3D12VertexBuffer ────────────────────────────────────────────────
		bool D3D12VertexBuffer::Create(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			Release();
			const uint32_t byteSize = stride * vertexNum;
			if (!CreateUploadBuffer(byteSize, resource_, mapped_)) return false;

			if (data) memcpy(mapped_, data, byteSize);

			stride_   = stride;
			capacity_ = byteSize;
			view_.BufferLocation = resource_->GetGPUVirtualAddress();
			view_.SizeInBytes    = byteSize;
			view_.StrideInBytes  = stride;
			return true;
		}

		void D3D12VertexBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			stride_   = 0;
			capacity_ = 0;
			view_     = {};
		}

		bool D3D12VertexBuffer::Update(const void* data, uint32_t byteSize)
		{
			if (!mapped_ || !data || byteSize > capacity_) return false;
			memcpy(mapped_, data, byteSize);
			return true;
		}


		// ── D3D12IndexBuffer ─────────────────────────────────────────────────
		bool D3D12IndexBuffer::Create(uint32_t indexNum, const void* data)
		{
			Release();
			const uint32_t byteSize = sizeof(uint32_t) * indexNum;
			if (!CreateUploadBuffer(byteSize, resource_, mapped_)) return false;

			if (data) memcpy(mapped_, data, byteSize);

			format_   = IndexFormat::UInt32;
			capacity_ = byteSize;
			view_.BufferLocation = resource_->GetGPUVirtualAddress();
			view_.SizeInBytes    = byteSize;
			view_.Format         = DXGI_FORMAT_R32_UINT;
			return true;
		}

		bool D3D12IndexBuffer::CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data)
		{
			Release();
			const uint32_t byteSize = IndexFormatStride(format) * indexNum;
			if (!CreateUploadBuffer(byteSize, resource_, mapped_)) return false;

			if (data) memcpy(mapped_, data, byteSize);

			format_   = format;
			capacity_ = byteSize;
			view_.BufferLocation = resource_->GetGPUVirtualAddress();
			view_.SizeInBytes    = byteSize;
			view_.Format         = (format == IndexFormat::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
			return true;
		}

		void D3D12IndexBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			format_   = IndexFormat::UInt32;
			capacity_ = 0;
			view_     = {};
		}

		bool D3D12IndexBuffer::Update(const void* data, uint32_t byteSize)
		{
			if (!mapped_ || !data || byteSize > capacity_) return false;
			memcpy(mapped_, data, byteSize);
			return true;
		}


		// ── D3D12ConstantBuffer ──────────────────────────────────────────────
		bool D3D12ConstantBuffer::Create(const void* data, uint32_t size)
		{
			Release();
			dataSize_ = size;
			// CBV は 256 バイトアライメントが必須
			size_ = (size + 255u) & ~255u;
			if (!CreateUploadBuffer(size_, resource_, mapped_)) return false;

			if (data) memcpy(mapped_, data, size);
			return true;
		}

		void D3D12ConstantBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			size_     = 0;
			dataSize_ = 0;
		}

		void D3D12ConstantBuffer::Update(const void* data)
		{
			if (!mapped_ || !data) return;
			memcpy(mapped_, data, dataSize_);
		}

		D3D12_GPU_VIRTUAL_ADDRESS D3D12ConstantBuffer::GetGPUAddress() const
		{
			return resource_ ? resource_->GetGPUVirtualAddress() : 0;
		}
	}
}
