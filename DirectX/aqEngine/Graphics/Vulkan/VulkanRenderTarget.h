#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/Vulkan/VulkanResources.h"
#include "Graphics/IRenderTarget.h"
#include "Graphics/IUnorderedAccessView.h"

namespace aq
{
	namespace graphics
	{
		class VulkanGraphicsDeviceImpl;

		// ── Vulkan レンダーターゲット (Phase 1b: swapchain proxy / Phase 3: オフスクリーン色+深度) ──
		// 2 モード:
		//   (a) swapchain proxy   … device の現在スワップチェーン画像へ解決 (メイン RT)
		//   (b) オフスクリーン     … 自前 VkImage 所有 (color + 任意 depth)。GBuffer/ポスプロ/メイン RT。
		// オフスクリーンのカラーは後続パスでサンプルするため SRV を内蔵し、
		// レイアウト (COLOR_ATTACHMENT ↔ SHADER_READ_ONLY) を追跡する。
		class VulkanRenderTarget : public IRenderTarget
		{
		public:
			~VulkanRenderTarget() override { Release(); }

			void InitAsSwapchainProxy(VulkanGraphicsDeviceImpl* device) { device_ = device; proxy_ = true; }
			bool CreateOffscreen(uint32_t width, uint32_t height, VkFormat colorFormat, bool hasDepth);
			void Release();

			bool IsProxy() const { return proxy_; }

			// カラー (proxy はフレーム毎に device の現在画像へ解決)
			VkImageView    GetView() const;
			VkImage        GetImage() const;
			VkFormat       GetFormat() const;
			VkExtent2D     GetExtent() const;
			VkImageLayout* ColorLayoutPtr() { return &colorLayout_; }

			// 深度 (オフスクリーンで hasDepth のときのみ)
			bool           HasDepth() const     { return depthImage_ != VK_NULL_HANDLE; }
			VkImageView    GetDepthView() const { return depthView_; }
			VkImage        GetDepthImage() const{ return depthImage_; }
			VkFormat       GetDepthFormat() const { return depthFormat_; }
			VkImageLayout* DepthLayoutPtr()     { return &depthLayout_; }

			IShaderResourceView&  GetRenderTargetSRV() override;  // 内蔵カラー SRV
			IUnorderedAccessView& GetRenderTargetUAV() override;  // Phase 4 (compute) assert

		private:
			// カラーをサンプルするための内蔵 SRV。元画像とレイアウト状態を context のバリアへ公開する。
			class ColorSRV final : public VulkanSRV
			{
			public:
				VulkanRenderTarget* owner = nullptr;
				VkImageView GetImageView() const override { return owner ? owner->GetView() : VK_NULL_HANDLE; }
				VkImage     GetImage() const override     { return owner ? owner->GetImage() : VK_NULL_HANDLE; }
				VkImageLayout* LayoutPtr() const override { return owner ? &owner->colorLayout_ : nullptr; }
				void* GetNativeHandle() const override    { return const_cast<ColorSRV*>(this); }
				void  Release() override {}  // owner が所有
			};

			VulkanGraphicsDeviceImpl* device_ = nullptr;
			bool                      proxy_  = false;

			VkImage       colorImage_  = VK_NULL_HANDLE;
			VmaAllocation colorAlloc_  = VK_NULL_HANDLE;
			VkImageView   colorView_   = VK_NULL_HANDLE;
			VkFormat      colorFormat_ = VK_FORMAT_UNDEFINED;
			VkImageLayout colorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
			VkExtent2D    extent_      = { 0, 0 };
			ColorSRV      colorSRV_;

			// compute が RT へ書き込む UAV (storage image, Phase 4)。
			class ColorUAV final : public VulkanUAV
			{
			public:
				VulkanRenderTarget* owner = nullptr;
				VkImageView GetStorageView() const override { return owner ? owner->storageView_ : VK_NULL_HANDLE; }
				VkImage     GetImage() const override       { return owner ? owner->colorImage_ : VK_NULL_HANDLE; }
				VkImageLayout* LayoutPtr() const override   { return owner ? &owner->colorLayout_ : nullptr; }
				void Release() override {}
			};
			ColorUAV      colorUAV_;
			VkImageView   storageView_ = VK_NULL_HANDLE;

			VkImage       depthImage_  = VK_NULL_HANDLE;
			VmaAllocation depthAlloc_  = VK_NULL_HANDLE;
			VkImageView   depthView_   = VK_NULL_HANDLE;
			VkFormat      depthFormat_ = VK_FORMAT_UNDEFINED;
			VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
		};
	}
}
