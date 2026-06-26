#pragma once
#include "D3D12Common.h"
#include "Graphics/IBuffer.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 バッファ群 (Phase 1: アップロードヒープ常駐の単純実装) ──
		// CPU から永続 Map して memcpy するシンプルな構成。
		//
		// frames-in-flight 対応:
		//   動的バッファ (毎フレーム Update する VB/IB / 全 CB) は内部で D3D12_FRAME_COUNT 本の
		//   領域をリングし、現在フレームの領域へ書き込み・参照する。これにより GPU が前フレームの
		//   データを読んでいる間に CPU が上書きする競合を防ぐ (呼び出し側は変更不要)。
		//   静的バッファ (メッシュ等・生成時に一度だけ書込) はリングしない (領域 1 本)。

		class D3D12VertexBuffer : public IVertexBuffer
		{
		private:
			ID3D12Resource*          resource_      = nullptr;
			uint8_t*                 mapped_        = nullptr;
			uint32_t                 stride_        = 0;
			uint32_t                 bytesPerFrame_ = 0;     // 1 フレーム分のバイト数 (確保容量)
			bool                     dynamic_       = false;
			mutable D3D12_VERTEX_BUFFER_VIEW view_  = {};    // GetView() で現在フレーム用に更新

		public:
			D3D12VertexBuffer()           = default;
			~D3D12VertexBuffer() override { Release(); }

			bool     Create(uint32_t vertexNum, uint32_t stride, const void* data) override;  // 静的
			bool     CreateDynamic(uint32_t vertexNum, uint32_t stride, const void* data);     // 動的(リング)
			void     Release() override;
			uint32_t GetStride() const override { return stride_; }
			bool     Update(const void* data, uint32_t byteSize) override;

			const D3D12_VERTEX_BUFFER_VIEW& GetView() const;

		private:
			bool CreateInternal(uint32_t byteSize, uint32_t stride, const void* data, bool dynamic);
		};


		class D3D12IndexBuffer : public IIndexBuffer
		{
		private:
			ID3D12Resource*         resource_      = nullptr;
			uint8_t*                mapped_        = nullptr;
			IndexFormat             format_        = IndexFormat::UInt32;
			uint32_t                bytesPerFrame_ = 0;     // 1 フレーム分のバイト数
			bool                    dynamic_       = false;
			mutable D3D12_INDEX_BUFFER_VIEW view_  = {};

		public:
			D3D12IndexBuffer()           = default;
			~D3D12IndexBuffer() override { Release(); }

			bool        Create(uint32_t indexNum, const void* data) override;  // 静的 R32
			bool        CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data);  // 動的(リング)
			void        Release() override;
			bool        Update(const void* data, uint32_t byteSize) override;
			IndexFormat GetFormat() const override { return format_; }

			const D3D12_INDEX_BUFFER_VIEW& GetView() const;
		};


		class D3D12ConstantBuffer : public IConstantBuffer
		{
		private:
			ID3D12Resource* resource_  = nullptr;
			uint8_t*        mapped_    = nullptr;
			uint32_t        alignedSize_ = 0;  // 256 アライン済みの 1 フレーム分サイズ
			uint32_t        dataSize_  = 0;     // 元データサイズ (Update の memcpy 量)

		public:
			D3D12ConstantBuffer()           = default;
			~D3D12ConstantBuffer() override { Release(); }

			bool Create(const void* data, uint32_t size) override;
			void Release() override;
			void Update(const void* data);  // 現在フレームの領域へ dataSize_ バイト memcpy

			D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;  // 現在フレームの領域アドレス
		};
	}
}
