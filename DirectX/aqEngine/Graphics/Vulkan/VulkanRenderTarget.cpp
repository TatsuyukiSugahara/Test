#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanRenderTarget.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include <vma/vk_mem_alloc.h>

namespace aq
{
	namespace graphics
	{
		bool VulkanRenderTarget::CreateOffscreen(uint32_t width, uint32_t height, VkFormat colorFormat, bool hasDepth)
		{
			Release();
			proxy_ = false;
			device_ = VulkanGraphicsDeviceImpl::GetInstance();
			if (!device_) return false;
			VkDevice     device    = device_->GetDevice();
			VmaAllocator allocator = device_->GetAllocator();

			colorFormat_ = colorFormat;
			extent_      = { width, height };
			colorSRV_.owner = this;
			colorUAV_.owner = this;

			VmaAllocationCreateInfo aci{};
			aci.usage = VMA_MEMORY_USAGE_AUTO;

			// カラー: COLOR_ATTACHMENT | SAMPLED | STORAGE(compute書込) | TRANSFER_SRC(CopyToBackBuffer)
			VkImageCreateInfo cci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			cci.imageType   = VK_IMAGE_TYPE_2D;
			cci.format      = colorFormat;
			cci.extent      = { width, height, 1 };
			cci.mipLevels   = 1;
			cci.arrayLayers = 1;
			cci.samples     = VK_SAMPLE_COUNT_1_BIT;
			cci.tiling      = VK_IMAGE_TILING_OPTIMAL;
			cci.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			if (!VK_VERIFY(vmaCreateImage(allocator, &cci, &aci, &colorImage_, &colorAlloc_, nullptr))) return false;

			VkImageViewCreateInfo cvi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			cvi.image    = colorImage_;
			cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
			cvi.format   = colorFormat;
			cvi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			if (!VK_VERIFY(vkCreateImageView(device, &cvi, nullptr, &colorView_))) return false;
			// storage 用ビュー (RWTexture2D)。同フォーマットの 2D ビュー。
			if (!VK_VERIFY(vkCreateImageView(device, &cvi, nullptr, &storageView_))) return false;

			if (hasDepth)
			{
				depthFormat_ = VK_FORMAT_D32_SFLOAT;
				VkImageCreateInfo dci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
				dci.imageType   = VK_IMAGE_TYPE_2D;
				dci.format      = depthFormat_;
				dci.extent      = { width, height, 1 };
				dci.mipLevels   = 1;
				dci.arrayLayers = 1;
				dci.samples     = VK_SAMPLE_COUNT_1_BIT;
				dci.tiling      = VK_IMAGE_TILING_OPTIMAL;
				dci.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				if (!VK_VERIFY(vmaCreateImage(allocator, &dci, &aci, &depthImage_, &depthAlloc_, nullptr))) return false;

				VkImageViewCreateInfo dvi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				dvi.image    = depthImage_;
				dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
				dvi.format   = depthFormat_;
				dvi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
				if (!VK_VERIFY(vkCreateImageView(device, &dvi, nullptr, &depthView_))) return false;
			}
			return true;
		}

		void VulkanRenderTarget::Release()
		{
			if (proxy_) { device_ = nullptr; proxy_ = false; return; }
			auto* dev = VulkanGraphicsDeviceImpl::GetInstance();
			if (!dev) return;
			VkDevice device = dev->GetDevice();
			VmaAllocator allocator = dev->GetAllocator();
			if (depthView_)   vkDestroyImageView(device, depthView_, nullptr);
			if (depthImage_)  vmaDestroyImage(allocator, depthImage_, depthAlloc_);
			if (storageView_) vkDestroyImageView(device, storageView_, nullptr);
			if (colorView_)   vkDestroyImageView(device, colorView_, nullptr);
			if (colorImage_)  vmaDestroyImage(allocator, colorImage_, colorAlloc_);
			storageView_ = VK_NULL_HANDLE;
			depthView_ = colorView_ = VK_NULL_HANDLE;
			depthImage_ = colorImage_ = VK_NULL_HANDLE;
			depthAlloc_ = colorAlloc_ = VK_NULL_HANDLE;
		}

		VkImageView VulkanRenderTarget::GetView() const
		{
			if (proxy_) return device_ ? device_->GetCurrentSwapchainView() : VK_NULL_HANDLE;
			return colorView_;
		}
		VkImage VulkanRenderTarget::GetImage() const
		{
			if (proxy_) return device_ ? device_->GetCurrentSwapchainImage() : VK_NULL_HANDLE;
			return colorImage_;
		}
		VkFormat VulkanRenderTarget::GetFormat() const
		{
			if (proxy_) return device_ ? device_->GetSwapchainFormat() : VK_FORMAT_UNDEFINED;
			return colorFormat_;
		}
		VkExtent2D VulkanRenderTarget::GetExtent() const
		{
			if (proxy_) return device_ ? device_->GetSwapchainExtent() : VkExtent2D{ 0, 0 };
			return extent_;
		}

		IShaderResourceView& VulkanRenderTarget::GetRenderTargetSRV()
		{
			return colorSRV_;  // オフスクリーンカラーをサンプルする SRV (proxy では未使用)
		}
		IUnorderedAccessView& VulkanRenderTarget::GetRenderTargetUAV()
		{
			return colorUAV_;  // storage image UAV (compute 書込)
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
