#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IBuffer.h"

namespace aq
{
	namespace graphics
	{
		// ── Vulkan バッファ群 (Phase 1a: VMA / HOST_VISIBLE 永続 Map の単純実装) ──
		// D3D12 Phase 1 と同じ思想: CPU から永続 Map して memcpy する。
		// frames-in-flight 対応: 動的バッファ (毎フレーム Update する VB/IB / 全 CB) は
		//   1 本のバッファ内に FRAME_COUNT 個の領域を確保しリングする。現在フレームの領域へ
		//   書き込み・参照することで GPU が前フレームを読む間の上書き競合を防ぐ。
		//   静的バッファ (メッシュ等) はリングしない (領域 1 本)。

		// 共通の VMA バッファ確保ヘルパ (HOST_VISIBLE | HOST_COHERENT, 永続 Map)。
		struct VulkanBufferAlloc
		{
			VkBuffer      buffer = VK_NULL_HANDLE;
			VmaAllocation alloc  = VK_NULL_HANDLE;
			uint8_t*      mapped = nullptr;

			bool Create(VkDeviceSize size, VkBufferUsageFlags usage);
			void Destroy();
		};


		class VulkanVertexBuffer : public IVertexBuffer
		{
		public:
			VulkanVertexBuffer() = default;
			~VulkanVertexBuffer() override { Release(); }

			bool     Create(uint32_t vertexNum, uint32_t stride, const void* data) override;  // 静的
			bool     CreateDynamic(uint32_t vertexNum, uint32_t stride, const void* data);     // 動的(リング)
			void     Release() override;
			uint32_t GetStride() const override { return stride_; }
			bool     Update(const void* data, uint32_t byteSize) override;

			VkBuffer     GetBuffer() const { return buf_.buffer; }
			VkDeviceSize GetCurrentOffset() const;  // 現在フレーム領域の先頭バイトオフセット

		private:
			bool CreateInternal(uint32_t byteSize, uint32_t stride, const void* data, bool dynamic);

			VulkanBufferAlloc buf_;
			uint32_t          stride_        = 0;
			uint32_t          bytesPerFrame_ = 0;
			bool              dynamic_       = false;
		};


		class VulkanIndexBuffer : public IIndexBuffer
		{
		public:
			VulkanIndexBuffer() = default;
			~VulkanIndexBuffer() override { Release(); }

			bool        Create(uint32_t indexNum, const void* data) override;  // 静的 R32
			bool        CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data);  // 動的(リング)
			void        Release() override;
			bool        Update(const void* data, uint32_t byteSize) override;
			IndexFormat GetFormat() const override { return format_; }

			VkBuffer     GetBuffer() const   { return buf_.buffer; }
			VkIndexType  GetIndexType() const;
			VkDeviceSize GetCurrentOffset() const;

		private:
			VulkanBufferAlloc buf_;
			IndexFormat       format_        = IndexFormat::UInt32;
			uint32_t          bytesPerFrame_ = 0;
			bool              dynamic_       = false;
		};


		// 定数バッファ。同一フレーム内で複数回 Update される (オブジェクト毎の world 行列など) ため、
		// Update 毎に別スライスへ bump 確保する。各フレーム領域に maxUpdates_ スロットを持ち、
		// フレーム先頭でカーソルをリセットする (frames-in-flight 競合回避)。
		class VulkanConstantBuffer : public IConstantBuffer
		{
		public:
			VulkanConstantBuffer() = default;
			~VulkanConstantBuffer() override { Release(); }

			bool Create(const void* data, uint32_t size) override;
			void Release() override;
			void Update(const void* data);  // 新スライスへ dataSize_ バイト memcpy し currentOffset_ 更新

			VkBuffer     GetBuffer() const { return buf_.buffer; }
			VkDeviceSize GetCurrentOffset() const { return currentOffset_; }
			uint32_t     GetRange() const { return dataSize_; }

		private:
			VulkanBufferAlloc buf_;
			uint32_t          alignedSize_ = 0;  // 256 整列の 1 スロット分
			uint32_t          dataSize_    = 0;  // 元データサイズ (Update の memcpy 量)
			uint32_t          maxUpdates_  = 0;  // 1 フレームあたりの Update スロット数
			uint32_t          lastFrame_   = 0xFFFFFFFFu;
			uint32_t          cursor_      = 0;  // 現フレーム内の Update カウンタ
			VkDeviceSize      currentOffset_ = 0;
		};
	}
}
