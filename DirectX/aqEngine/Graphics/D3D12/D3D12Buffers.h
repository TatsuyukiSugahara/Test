#pragma once
#include "D3D12Common.h"
#include "Graphics/IBuffer.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 バッファ群 (Phase 1: アップロードヒープ常駐の単純実装) ──
		// CPU から永続 Map して memcpy するシンプルな構成。
		// DEFAULT ヒープ + CopyBufferRegion による最適化は後続フェーズで行う。

		class D3D12VertexBuffer : public IVertexBuffer
		{
		private:
			ID3D12Resource*          resource_ = nullptr;
			void*                    mapped_   = nullptr;
			uint32_t                 stride_   = 0;
			uint32_t                 capacity_ = 0;  // bytes
			D3D12_VERTEX_BUFFER_VIEW view_     = {};

		public:
			D3D12VertexBuffer()           = default;
			~D3D12VertexBuffer() override { Release(); }

			bool     Create(uint32_t vertexNum, uint32_t stride, const void* data) override;
			void     Release() override;
			uint32_t GetStride() const override { return stride_; }
			bool     Update(const void* data, uint32_t byteSize) override;

			const D3D12_VERTEX_BUFFER_VIEW& GetView() const { return view_; }
		};


		class D3D12IndexBuffer : public IIndexBuffer
		{
		private:
			ID3D12Resource*         resource_ = nullptr;
			void*                   mapped_   = nullptr;
			IndexFormat             format_   = IndexFormat::UInt32;
			uint32_t                capacity_ = 0;  // bytes
			D3D12_INDEX_BUFFER_VIEW view_     = {};

		public:
			D3D12IndexBuffer()           = default;
			~D3D12IndexBuffer() override { Release(); }

			bool        Create(uint32_t indexNum, const void* data) override;  // 静的 R32
			bool        CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data);
			void        Release() override;
			bool        Update(const void* data, uint32_t byteSize) override;
			IndexFormat GetFormat() const override { return format_; }

			const D3D12_INDEX_BUFFER_VIEW& GetView() const { return view_; }
		};


		class D3D12ConstantBuffer : public IConstantBuffer
		{
		private:
			ID3D12Resource* resource_ = nullptr;
			void*           mapped_   = nullptr;
			uint32_t        size_     = 0;  // 256 アライン済みバイト数 (バッファ確保サイズ)
			uint32_t        dataSize_ = 0;  // 元データサイズ (Update の memcpy 量)

		public:
			D3D12ConstantBuffer()           = default;
			~D3D12ConstantBuffer() override { Release(); }

			bool Create(const void* data, uint32_t size) override;
			void Release() override;
			void Update(const void* data);  // mapped_ へ dataSize_ バイト memcpy

			D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;
			uint32_t                  GetSize() const { return size_; }
		};
	}
}
