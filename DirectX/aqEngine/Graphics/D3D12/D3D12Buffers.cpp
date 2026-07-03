#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12Buffers.h"
#include "D3D12GraphicsDeviceImpl.h"
#include <algorithm>


namespace aq
{
	namespace graphics
	{
		namespace
		{
			// アップロードヒープ上にバッファリソースを作成し、永続 Map する。
			bool CreateUploadBuffer(uint32_t byteSize, ID3D12Resource*& outResource, uint8_t*& outMapped)
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
				void* mapped = nullptr;
				hr = outResource->Map(0, &readRange, &mapped);
				if (FAILED(hr))
				{
					SafeReleaseD3D12(outResource);
					return false;
				}
				outMapped = static_cast<uint8_t*>(mapped);
				return true;
			}

			uint32_t IndexFormatStride(IndexFormat format)
			{
				return format == IndexFormat::UInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
			}
		}


		// ── D3D12VertexBuffer ────────────────────────────────────────────────
		bool D3D12VertexBuffer::CreateInternal(uint32_t byteSize, uint32_t stride, const void* data, bool dynamic)
		{
			Release();
			const uint32_t frames = dynamic ? D3D12_FRAME_COUNT : 1;
			if (!CreateUploadBuffer(byteSize * frames, resource_, mapped_)) return false;

			stride_        = stride;
			bytesPerFrame_ = byteSize;
			dynamic_       = dynamic;
			if (data)
				for (uint32_t f = 0; f < frames; ++f)  // 全フレーム領域へ初期データを複製
					memcpy(mapped_ + f * byteSize, data, byteSize);
			return true;
		}

		bool D3D12VertexBuffer::Create(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			return CreateInternal(stride * vertexNum, stride, data, /*dynamic*/false);
		}

		bool D3D12VertexBuffer::CreateDynamic(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			return CreateInternal(stride * vertexNum, stride, data, /*dynamic*/true);
		}

		void D3D12VertexBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			stride_ = 0;
			bytesPerFrame_ = 0;
			dynamic_ = false;
			view_ = {};
		}

		bool D3D12VertexBuffer::Update(const void* data, uint32_t byteSize)
		{
			if (!mapped_ || !data || byteSize > bytesPerFrame_) return false;
			const uint32_t frame = dynamic_ ? D3D12GraphicsDeviceImpl::GetStaticFrameIndex() : 0;
			memcpy(mapped_ + static_cast<size_t>(frame) * bytesPerFrame_, data, byteSize);
			return true;
		}

		const D3D12_VERTEX_BUFFER_VIEW& D3D12VertexBuffer::GetView() const
		{
			const uint32_t frame = dynamic_ ? D3D12GraphicsDeviceImpl::GetStaticFrameIndex() : 0;
			view_.BufferLocation = resource_->GetGPUVirtualAddress() + static_cast<UINT64>(frame) * bytesPerFrame_;
			view_.SizeInBytes    = bytesPerFrame_;
			view_.StrideInBytes  = stride_;
			return view_;
		}


		// ── D3D12IndexBuffer ─────────────────────────────────────────────────
		bool D3D12IndexBuffer::Create(uint32_t indexNum, const void* data)
		{
			Release();
			const uint32_t byteSize = sizeof(uint32_t) * indexNum;
			if (!CreateUploadBuffer(byteSize, resource_, mapped_)) return false;
			if (data) memcpy(mapped_, data, byteSize);
			format_        = IndexFormat::UInt32;
			bytesPerFrame_ = byteSize;
			dynamic_       = false;
			return true;
		}

		bool D3D12IndexBuffer::CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data)
		{
			Release();
			const uint32_t byteSize = IndexFormatStride(format) * indexNum;
			if (!CreateUploadBuffer(byteSize * D3D12_FRAME_COUNT, resource_, mapped_)) return false;
			format_        = format;
			bytesPerFrame_ = byteSize;
			dynamic_       = true;
			if (data)
				for (uint32_t f = 0; f < D3D12_FRAME_COUNT; ++f)
					memcpy(mapped_ + f * byteSize, data, byteSize);
			return true;
		}

		void D3D12IndexBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			format_ = IndexFormat::UInt32;
			bytesPerFrame_ = 0;
			dynamic_ = false;
			view_ = {};
		}

		bool D3D12IndexBuffer::Update(const void* data, uint32_t byteSize)
		{
			if (!mapped_ || !data || byteSize > bytesPerFrame_) return false;
			const uint32_t frame = dynamic_ ? D3D12GraphicsDeviceImpl::GetStaticFrameIndex() : 0;
			memcpy(mapped_ + static_cast<size_t>(frame) * bytesPerFrame_, data, byteSize);
			return true;
		}

		const D3D12_INDEX_BUFFER_VIEW& D3D12IndexBuffer::GetView() const
		{
			const uint32_t frame = dynamic_ ? D3D12GraphicsDeviceImpl::GetStaticFrameIndex() : 0;
			view_.BufferLocation = resource_->GetGPUVirtualAddress() + static_cast<UINT64>(frame) * bytesPerFrame_;
			view_.SizeInBytes    = bytesPerFrame_;
			view_.Format         = (format_ == IndexFormat::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
			return view_;
		}


		// ── D3D12ConstantBuffer ──────────────────────────────────────────────
		// 全 CB は毎フレーム Update されるため常にフレームリングする (256B アライン領域 × FRAME_COUNT)。
		bool D3D12ConstantBuffer::Create(const void* data, uint32_t size)
		{
			Release();
			dataSize_    = size;
			alignedSize_ = (size + 255u) & ~255u;  // CBV は 256B アライン必須
			if (!CreateUploadBuffer(alignedSize_ * D3D12_FRAME_COUNT, resource_, mapped_)) return false;
			if (data)
				for (uint32_t f = 0; f < D3D12_FRAME_COUNT; ++f)
					memcpy(mapped_ + f * alignedSize_, data, size);
			return true;
		}

		void D3D12ConstantBuffer::Release()
		{
			if (resource_ && mapped_) resource_->Unmap(0, nullptr);
			mapped_ = nullptr;
			SafeReleaseD3D12(resource_);
			alignedSize_ = 0;
			dataSize_    = 0;
		}

		void D3D12ConstantBuffer::Update(const void* data)
		{
			if (!mapped_ || !data) return;
			const uint32_t frame = D3D12GraphicsDeviceImpl::GetStaticFrameIndex();
			memcpy(mapped_ + static_cast<size_t>(frame) * alignedSize_, data, dataSize_);
		}

		D3D12_GPU_VIRTUAL_ADDRESS D3D12ConstantBuffer::GetGPUAddress() const
		{
			if (!resource_) return 0;
			const uint32_t frame = D3D12GraphicsDeviceImpl::GetStaticFrameIndex();
			return resource_->GetGPUVirtualAddress() + static_cast<UINT64>(frame) * alignedSize_;
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D12
