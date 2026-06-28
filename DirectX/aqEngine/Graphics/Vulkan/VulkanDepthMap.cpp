#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanDepthMap.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include <vma/vk_mem_alloc.h>

namespace aq
{
	namespace graphics
	{
		bool VulkanDepthMap::Create(uint32_t resolution)
		{
			Release();
			auto* dev = VulkanGraphicsDeviceImpl::GetInstance();
			if (!dev) return false;
			VkDevice     device    = dev->GetDevice();
			VmaAllocator allocator = dev->GetAllocator();
			resolution_ = resolution;

			// D32_SFLOAT Texture2DArray(4)。DEPTH_STENCIL_ATTACHMENT | SAMPLED。
			VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			ici.imageType   = VK_IMAGE_TYPE_2D;
			ici.format      = VK_FORMAT_D32_SFLOAT;
			ici.extent      = { resolution, resolution, 1 };
			ici.mipLevels   = 1;
			ici.arrayLayers = kArraySize;
			ici.samples     = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
			ici.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;  // 初期 1.0 クリア用
			VmaAllocationCreateInfo aci{};
			aci.usage = VMA_MEMORY_USAGE_AUTO;
			if (!VK_VERIFY(vmaCreateImage(allocator, &ici, &aci, &image_, &alloc_, nullptr))) return false;

			// 全スライス配列 SRV ビュー
			VkImageViewCreateInfo avi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			avi.image    = image_;
			avi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			avi.format   = VK_FORMAT_D32_SFLOAT;
			avi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kArraySize };
			if (!VK_VERIFY(vkCreateImageView(device, &avi, nullptr, &arrayView_))) return false;
			arraySRV_.owner = this;
			arraySRV_.view  = arrayView_;

			// スライス別ビュー (DSV attachment / デバッグ SRV)
			for (uint32_t s = 0; s < kArraySize; ++s)
			{
				VkImageViewCreateInfo svi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				svi.image    = image_;
				svi.viewType = VK_IMAGE_VIEW_TYPE_2D;
				svi.format   = VK_FORMAT_D32_SFLOAT;
				svi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
				if (!VK_VERIFY(vkCreateImageView(device, &svi, nullptr, &sliceViews_[s]))) return false;
				sliceSRVs_[s].owner = this;
				sliceSRVs_[s].view  = sliceViews_[s];
			}

			// 比較サンプラー (シャドウ SampleCmp 用 LESS_OR_EQUAL)
			SamplerDesc sd;
			sd.filter       = FilterMode::MinMagMipLinear;
			sd.addressU = sd.addressV = sd.addressW = AddressMode::Border;
			sd.isComparison = true;
			sampler_.Create(sd);

			// シャドウパス未実装(Phase 4)の間、深度を 1.0(遠=影なし)でクリアし SHADER_READ_ONLY にしておく。
			// これで未描画でもライティングが「影なし」になり、ジオメトリが見えるようになる。
			dev->ImmediateSubmit([&](VkCommandBuffer cmd)
			{
				auto barrier = [&](VkImageLayout oldL, VkImageLayout newL, VkAccessFlags2 sa, VkAccessFlags2 da)
				{
					VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
					b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; b.srcAccessMask = sa;
					b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; b.dstAccessMask = da;
					b.oldLayout = oldL; b.newLayout = newL; b.image = image_;
					b.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kArraySize };
					VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
					dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &b;
					vkCmdPipelineBarrier2(cmd, &dep);
				};
				barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_2_TRANSFER_WRITE_BIT);
				VkClearDepthStencilValue clear{ 1.0f, 0 };
				VkImageSubresourceRange range{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kArraySize };
				vkCmdClearDepthStencilImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
				barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			});
			layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			return true;
		}

		void VulkanDepthMap::Release()
		{
			auto* dev = VulkanGraphicsDeviceImpl::GetInstance();
			sampler_.Release();
			if (!dev) { image_ = VK_NULL_HANDLE; return; }
			VkDevice device = dev->GetDevice();
			for (auto& v : sliceViews_) if (v) { vkDestroyImageView(device, v, nullptr); v = VK_NULL_HANDLE; }
			if (arrayView_) { vkDestroyImageView(device, arrayView_, nullptr); arrayView_ = VK_NULL_HANDLE; }
			if (image_)     { vmaDestroyImage(dev->GetAllocator(), image_, alloc_); image_ = VK_NULL_HANDLE; alloc_ = VK_NULL_HANDLE; }
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
