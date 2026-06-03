#pragma once
#include "IBuffer.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"


namespace engine
{
	namespace graphics
	{
		/**
		 * D3D11 バッファ基底クラス (ID3D11Buffer を保持)
		 */
		class GPUBuffer
		{
		private:
			ID3D11Buffer* gpuBuffer_;

		public:
			GPUBuffer();
			virtual ~GPUBuffer();

			bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);

			inline ID3D11Buffer*& GetBody()         { return gpuBuffer_; }
			inline void*          GetNativeHandle() const { return static_cast<void*>(gpuBuffer_); }

			void Release();
		};




		/*******************************************/


		/**
		 * 定数バッファ (D3D11)
		 */
		class ConstantBuffer : public GPUBuffer, public IConstantBuffer
		{
		public:
			ConstantBuffer();
			virtual ~ConstantBuffer();

			bool Create(const void* initialData, uint32_t bufferSize) override;
			void Release() override       { GPUBuffer::Release(); }
			void* GetNativeHandle() const override { return GPUBuffer::GetNativeHandle(); }
		};




		/*******************************************/


		/**
		 * 構造化バッファ (D3D11)
		 */
		class StructuredBuffer : public IStructuredBuffer
		{
		private:
			ID3D11Buffer* structuredBuffer_;

		public:
			StructuredBuffer();
			~StructuredBuffer();

			bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);

			inline ID3D11Buffer*& GetBody()         { return structuredBuffer_; }
			inline void*          GetNativeHandle() const override { return static_cast<void*>(structuredBuffer_); }

			void Release() override;
		};




		/*******************************************/


		/**
		 * 頂点バッファ (D3D11)
		 */
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

			inline ID3D11Buffer*& GetBody()         { return vertexBuffer_; }
			inline void*          GetNativeHandle() const override { return static_cast<void*>(vertexBuffer_); }
		};




		/*******************************************/


		/**
		 * インデックスバッファ (D3D11)
		 */
		class IndexBuffer : public IIndexBuffer
		{
		private:
			ID3D11Buffer* indexBuffer_;

		public:
			IndexBuffer();
			~IndexBuffer();

			bool  Create(uint32_t indexNum, const void* srcIndexBuffer) override;
			void  Release() override;

			inline ID3D11Buffer*& GetBody()         { return indexBuffer_; }
			inline void*          GetNativeHandle() const override { return static_cast<void*>(indexBuffer_); }
		};




		/*******************************************/


	}
}
