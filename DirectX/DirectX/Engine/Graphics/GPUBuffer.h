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
			 * GPUBuffer����
			 */
			bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);

			/**
			 * ID3D11Buffer�擾
			 */
			inline ID3D11Buffer*& GetBody() { return gpuBuffer_; }

			/**
			 * ���
			 */
			void Release();
		};




		/*******************************************/


		/**
		 * �萔�o�b�t�@
		 */
		class ConstantBuffer : public GPUBuffer
		{
		public:
			ConstantBuffer();
			virtual ~ConstantBuffer();

			/**
			 * ConstantBuffer����
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

			/** StructuredBuffer���� */
			inline bool Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc);
			/** ID3D11Buffer���擾 */
			inline ID3D11Buffer*& GetBody() { return structuredBuffer_; }
			/** ��� */
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

			/** VertexBuffer���� */
			bool Create(uint32_t vertexNum, uint32_t stride, const void* srcVertexBuffer);
			/** ���_�X�g���C�h�擾 */
			inline uint32_t GetStride() const { return stride_; }
			/** ID3D11Buffer���擾 */
			inline ID3D11Buffer*& GetBody() { return vertexBuffer_; }
			/** ��� */
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

			/** IndexBuffer���� */
			bool Create(uint32_t indexNum, const void* srcIndexBuffer);
			/** ID3D11Buffer���擾 */
			inline ID3D11Buffer*& GetBody() { return indexBuffer_; }
			/** ��� */
			void Release();
		};




		/*******************************************/


		/**
		 * ���_�o�b�t�@�̏�� 
		 */
		struct VertexData
		{
			math::Vector3 position;
			math::Vector3 normal;
			math::Vector2 uv;
		};


		/**
		 * �萔�o�b�t�@
		 */
		struct VSConstantBuffer
		{
			math::Matrix4x4 world;
			math::Matrix4x4 view;
			math::Matrix4x4 projection;
		};
	}
}