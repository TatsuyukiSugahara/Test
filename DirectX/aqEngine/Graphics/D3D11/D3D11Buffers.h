#pragma once
#include "Graphics/IBuffer.h"

namespace aq
{
	namespace graphics
	{
		class GPUBuffer
		{
		private:
			ID3D11Buffer* gpuBuffer_;

		public:
			GPUBuffer();
			virtual ~GPUBuffer();

			bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);

			inline ID3D11Buffer*& GetBody() { return gpuBuffer_; }

			void Release();
		};

		class ConstantBuffer : public GPUBuffer, public IConstantBuffer
		{
		public:
			ConstantBuffer();
			virtual ~ConstantBuffer();

			bool Create(const void* initialData, uint32_t bufferSize) override;
			void Release() override { GPUBuffer::Release(); }
		};

		class StructuredBuffer : public IStructuredBuffer
		{
		private:
			ID3D11Buffer* structuredBuffer_;

		public:
			StructuredBuffer();
			~StructuredBuffer();

			bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);

			inline ID3D11Buffer*& GetBody() { return structuredBuffer_; }

			void Release() override;
		};

		class VertexBuffer : public IVertexBuffer
		{
		private:
			ID3D11Buffer* vertexBuffer_;
			uint32_t      stride_;

		public:
			VertexBuffer();
			~VertexBuffer();

			bool     Create(uint32_t vertexNum, uint32_t stride, const void* srcVertexBuffer) override;
			void     Release() override;
			uint32_t GetStride() const override { return stride_; }

			inline ID3D11Buffer*& GetBody() { return vertexBuffer_; }
		};

		/** DYNAMIC 頂点バッファ: Map/Unmap で毎フレーム書き換え可能 (ハイトマップペイント用) */
		class DynamicVertexBuffer : public IVertexBuffer
		{
		private:
			ID3D11Buffer* vb_       = nullptr;
			uint32_t      stride_   = 0;
			uint32_t      capacity_ = 0;   // bytes

		public:
			DynamicVertexBuffer()  = default;
			~DynamicVertexBuffer() { Release(); }

			bool     Create(uint32_t vertexNum, uint32_t stride, const void* data) override;
			void     Release() override;
			uint32_t GetStride() const override { return stride_; }
			bool     Update(const void* data, uint32_t byteSize) override;

			inline ID3D11Buffer*& GetBody() { return vb_; }
		};

		/** 静的 (R32_UINT) / 動的 (R16・R32) の両方に対応するインデックスバッファ。
		 *  Create()        : 静的 R32_UINT (メッシュ用、従来挙動)
		 *  CreateDynamic() : 動的、フォーマット指定可 (UI など毎フレーム書き換え用)
		 */
		class IndexBuffer : public IIndexBuffer
		{
		private:
			ID3D11Buffer* indexBuffer_ = nullptr;
			IndexFormat   format_      = IndexFormat::UInt32;
			uint32_t      capacity_    = 0;  // bytes。0 なら静的バッファ (Update 不可)

		public:
			IndexBuffer();
			~IndexBuffer();

			bool        Create(uint32_t indexNum, const void* srcIndexBuffer) override;
			bool        CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data);
			void        Release() override;
			bool        Update(const void* data, uint32_t byteSize) override;
			IndexFormat GetFormat() const override { return format_; }

			inline ID3D11Buffer*& GetBody() { return indexBuffer_; }
		};
	}
}
