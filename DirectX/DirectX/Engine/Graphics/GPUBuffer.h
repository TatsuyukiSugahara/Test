#pragma once

namespace engine
{
	namespace graphics
	{
		/**
		 * GPUBuffer
		 */
		class GPUBuffer
		{
		private:
			/** GPUBuffer */
			ID3D11Buffer* gpuBuffer_;


		public:
			GPUBuffer();
			virtual ~GPUBuffer();

			/**
			 * GPUBuffer生成
			 */
			bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);

			/**
			 * ID3D11Buffer取得
			 */
			inline ID3D11Buffer*& GetBody() { return gpuBuffer_; }

			/**
			 * 解放
			 */
			void Release();
		};




		/*******************************************/


		/**
		 * 定数バッファ
		 */
		class ConstantBuffer : public GPUBuffer
		{
		public:
			ConstantBuffer();
			virtual ~ConstantBuffer();

			/**
			 * ConstantBuffer生成
			 */
			bool Create(const void* initialData, uint32_t bufferSize);
		};




		/*******************************************/


		/**
		 * StructuredBuffer
		 */
		class StructuredBuffer
		{
		private:
			ID3D11Buffer* structuredBuffer_;

		public:
			StructuredBuffer();
			~StructuredBuffer();

			/** StructuredBuffer生成 */
			inline bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);
			/** ID3D11Bufferを取得 */
			inline ID3D11Buffer*& GetBody() { return structuredBuffer_; }
			/** 解放 */
			void Release();
		};




		/*******************************************/


		/**
		 * VertexBuffer
		 */
		class VertexBuffer
		{
		private:
			ID3D11Buffer* vertexBuffer_;
			uint32_t stride_;

		public:
			VertexBuffer();
			~VertexBuffer();

			/** VertexBuffer生成 */
			bool Create(uint32_t vertexNum, uint32_t stride, const void* srcVertexBuffer);
			/** 頂点ストライド取得 */
			inline uint32_t GetStride() const { return stride_; }
			/** ID3D11Bufferを取得 */
			inline ID3D11Buffer*& GetBody() { return vertexBuffer_; }
			/** 解放 */
			void Release();
		};




		/*******************************************/


		/**
		 * IndexBuffer
		 */
		class IndexBuffer
		{
		private:
			ID3D11Buffer* indexBuffer_;

		public:
			IndexBuffer();
			~IndexBuffer();

			/** IndexBuffer生成 */
			bool Create(uint32_t indexNum, const void* srcIndexBuffer);
			/** ID3D11Bufferを取得 */
			inline ID3D11Buffer*& GetBody() { return indexBuffer_; }
			/** 解放 */
			void Release();
		};




		/*******************************************/


		/**
		 * 頂点バッファの情報 
		 */
		struct VertexData
		{
			math::Vector3 position;
			math::Vector3 normal;
			math::Vector2 uv;
		};


		/**
		 * 定数バッファ
		 */
		struct VSConstantBuffer
		{
			math::Matrix4x4 world;
			math::Matrix4x4 view;
			math::Matrix4x4 projection;
		};
	}
}