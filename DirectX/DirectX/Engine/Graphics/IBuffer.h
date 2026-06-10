#pragma once
#include <cstdint>


namespace engine
{
	namespace graphics
	{
		/** 頂点バッファ インターフェース */
		class IVertexBuffer
		{
		public:
			virtual ~IVertexBuffer() = default;
			virtual bool     Create(uint32_t vertexNum, uint32_t stride, const void* data) = 0;
			virtual void     Release() = 0;
			virtual uint32_t GetStride() const = 0;
		};

		/** インデックスバッファ インターフェース */
		class IIndexBuffer
		{
		public:
			virtual ~IIndexBuffer() = default;
			virtual bool Create(uint32_t indexNum, const void* data) = 0;
			virtual void Release() = 0;
		};

		/** 定数バッファ インターフェース */
		class IConstantBuffer
		{
		public:
			virtual ~IConstantBuffer() = default;
			virtual bool Create(const void* data, uint32_t size) = 0;
			virtual void Release() = 0;
		};

		/** 構造体バッファ インターフェース */
		class IStructuredBuffer
		{
		public:
			virtual ~IStructuredBuffer() = default;
			virtual void Release() = 0;
		};
	}
}
