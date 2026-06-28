#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanBuffers.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include <vma/vk_mem_alloc.h>
#include <cstring>

namespace aq
{
	namespace graphics
	{
		namespace
		{
			// UBO オフセットアライン。Vulkan 仕様の minUniformBufferOffsetAlignment 上限は 256 なので
			// 256 整列なら全 GPU で安全 (D3D12 の CBV 256 整列と同じ)。
			constexpr uint32_t kUboAlign = 256;
			uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
		}

		// ── VulkanBufferAlloc ──────────────────────────────────
		bool VulkanBufferAlloc::Create(VkDeviceSize size, VkBufferUsageFlags usage)
		{
			Destroy();
			VmaAllocator allocator = VulkanGraphicsDeviceImpl::GetStaticAllocator();
			if (!allocator || size == 0) return false;

			VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			bi.size  = size;
			bi.usage = usage;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			// HOST_VISIBLE | HOST_COHERENT 永続 Map (シーケンシャル書き込み)。
			VmaAllocationCreateInfo ai{};
			ai.usage = VMA_MEMORY_USAGE_AUTO;
			ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			           VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VmaAllocationInfo info{};
			if (!VK_VERIFY(vmaCreateBuffer(allocator, &bi, &ai, &buffer, &alloc, &info)))
				return false;
			mapped = static_cast<uint8_t*>(info.pMappedData);
			return mapped != nullptr;
		}

		void VulkanBufferAlloc::Destroy()
		{
			if (buffer)
			{
				VmaAllocator allocator = VulkanGraphicsDeviceImpl::GetStaticAllocator();
				if (allocator) vmaDestroyBuffer(allocator, buffer, alloc);
			}
			buffer = VK_NULL_HANDLE;
			alloc  = VK_NULL_HANDLE;
			mapped = nullptr;
		}

		// ── VertexBuffer ───────────────────────────────────────
		bool VulkanVertexBuffer::CreateInternal(uint32_t byteSize, uint32_t stride, const void* data, bool dynamic)
		{
			stride_        = stride;
			dynamic_       = dynamic;
			bytesPerFrame_ = byteSize;
			const uint32_t frames = dynamic ? VulkanGraphicsDeviceImpl::GetFrameCount() : 1;
			if (!buf_.Create((VkDeviceSize)byteSize * frames, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
				return false;
			if (data)
			{
				// 全フレーム領域へ初期データを複製しておく (動的の未更新フレーム対策)。
				for (uint32_t f = 0; f < frames; ++f)
					std::memcpy(buf_.mapped + (size_t)byteSize * f, data, byteSize);
			}
			return true;
		}

		bool VulkanVertexBuffer::Create(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			return CreateInternal(vertexNum * stride, stride, data, false);
		}

		bool VulkanVertexBuffer::CreateDynamic(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			return CreateInternal(vertexNum * stride, stride, data, true);
		}

		bool VulkanVertexBuffer::Update(const void* data, uint32_t byteSize)
		{
			if (!dynamic_ || !buf_.mapped || byteSize > bytesPerFrame_) return false;
			std::memcpy(buf_.mapped + GetCurrentOffset(), data, byteSize);
			return true;
		}

		VkDeviceSize VulkanVertexBuffer::GetCurrentOffset() const
		{
			return dynamic_ ? (VkDeviceSize)bytesPerFrame_ * VulkanGraphicsDeviceImpl::GetStaticFrameIndex() : 0;
		}

		void VulkanVertexBuffer::Release() { buf_.Destroy(); }

		// ── IndexBuffer ────────────────────────────────────────
		bool VulkanIndexBuffer::Create(uint32_t indexNum, const void* data)
		{
			format_        = IndexFormat::UInt32;
			dynamic_       = false;
			bytesPerFrame_ = indexNum * 4;
			if (!buf_.Create(bytesPerFrame_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
			if (data) std::memcpy(buf_.mapped, data, bytesPerFrame_);
			return true;
		}

		bool VulkanIndexBuffer::CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data)
		{
			format_        = format;
			dynamic_       = true;
			const uint32_t elem = (format == IndexFormat::UInt16) ? 2u : 4u;
			bytesPerFrame_ = indexNum * elem;
			const uint32_t frames = VulkanGraphicsDeviceImpl::GetFrameCount();
			if (!buf_.Create((VkDeviceSize)bytesPerFrame_ * frames, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
			if (data)
				for (uint32_t f = 0; f < frames; ++f)
					std::memcpy(buf_.mapped + (size_t)bytesPerFrame_ * f, data, bytesPerFrame_);
			return true;
		}

		bool VulkanIndexBuffer::Update(const void* data, uint32_t byteSize)
		{
			if (!dynamic_ || !buf_.mapped || byteSize > bytesPerFrame_) return false;
			std::memcpy(buf_.mapped + GetCurrentOffset(), data, byteSize);
			return true;
		}

		VkIndexType VulkanIndexBuffer::GetIndexType() const
		{
			return (format_ == IndexFormat::UInt16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
		}

		VkDeviceSize VulkanIndexBuffer::GetCurrentOffset() const
		{
			return dynamic_ ? (VkDeviceSize)bytesPerFrame_ * VulkanGraphicsDeviceImpl::GetStaticFrameIndex() : 0;
		}

		void VulkanIndexBuffer::Release() { buf_.Destroy(); }

		// ── ConstantBuffer (Update 毎に別スライスへ bump 確保) ──────
		bool VulkanConstantBuffer::Create(const void* data, uint32_t size)
		{
			dataSize_    = size;
			alignedSize_ = AlignUp(size, kUboAlign);
			// 1 フレームあたりのスロット数。メモリ予算 ~256KB/フレーム領域に収まるよう調整 (最低 4)。
			maxUpdates_  = 262144u / alignedSize_;
			if (maxUpdates_ < 4)   maxUpdates_ = 4;
			if (maxUpdates_ > 1024) maxUpdates_ = 1024;
			const uint32_t frames = VulkanGraphicsDeviceImpl::GetFrameCount();
			const VkDeviceSize total = (VkDeviceSize)alignedSize_ * maxUpdates_ * frames;
			if (!buf_.Create(total, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) return false;
			currentOffset_ = 0;
			// 初期データを各フレーム領域の先頭スロットへ (Update されない静的 CB 用)。
			if (data)
				for (uint32_t f = 0; f < frames; ++f)
					std::memcpy(buf_.mapped + (size_t)alignedSize_ * maxUpdates_ * f, data, size);
			return true;
		}

		void VulkanConstantBuffer::Update(const void* data)
		{
			if (!buf_.mapped || !data) return;
			const uint32_t fi = VulkanGraphicsDeviceImpl::GetStaticFrameIndex();
			if (fi != lastFrame_) { lastFrame_ = fi; cursor_ = 0; }  // フレーム先頭でリセット
			const uint32_t slot = (cursor_ < maxUpdates_) ? cursor_ : (maxUpdates_ - 1);
			++cursor_;
			currentOffset_ = (VkDeviceSize)((size_t)fi * maxUpdates_ + slot) * alignedSize_;
			std::memcpy(buf_.mapped + currentOffset_, data, dataSize_);
		}

		void VulkanConstantBuffer::Release() { buf_.Destroy(); }
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
