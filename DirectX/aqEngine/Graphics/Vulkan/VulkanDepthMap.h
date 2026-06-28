#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/Vulkan/VulkanResources.h"
#include "Graphics/IDepthMap.h"

namespace aq
{
	namespace graphics
	{
		// ── Vulkan 深度専用テクスチャ (Phase 3) ──
		// D3D12DepthMap と同等: D32_SFLOAT の Texture2DArray(ArraySize=4)。
		// - スライス別 2D ビューを DSV 相当の attachment として使う (シャドウ書き込み=Phase 4)。
		// - 全スライス配列 SRV を PSt4 にバインド。比較サンプラー(LESS_OR_EQUAL)を内蔵。
		class VulkanDepthMap final : public IDepthMap
		{
		public:
			static constexpr uint32_t kArraySize = 4;

			~VulkanDepthMap() override { Release(); }

			bool Create(uint32_t resolution);
			void Release();

			// IDepthMap
			IShaderResourceView* GetSRV() const override { return const_cast<DepthSRV*>(&arraySRV_); }
			IShaderResourceView* GetSliceSRV(uint32_t slice) const override
			{
				return (slice < kArraySize) ? const_cast<DepthSRV*>(&sliceSRVs_[slice])
				                            : const_cast<DepthSRV*>(&arraySRV_);
			}
			ISamplerState* GetSampler()    const override { return const_cast<VulkanSampler*>(&sampler_); }
			uint32_t       GetResolution() const override { return resolution_; }

			// Vulkan 固有 (シャドウ書き込み=Phase 4)
			VkImageView    GetSliceView(uint32_t slice) const { return (slice < kArraySize) ? sliceViews_[slice] : VK_NULL_HANDLE; }
			VkImage        GetImage() const     { return image_; }
			VkImageLayout* LayoutPtr()          { return &layout_; }

		private:
			// SRV ホルダ (DEPTH aspect, layout 追跡)。
			class DepthSRV final : public VulkanSRV
			{
			public:
				VulkanDepthMap* owner = nullptr;
				VkImageView     view  = VK_NULL_HANDLE;
				VkImageView GetImageView() const override { return view; }
				VkImage     GetImage() const override     { return owner ? owner->image_ : VK_NULL_HANDLE; }
				VkImageLayout* LayoutPtr() const override { return owner ? &owner->layout_ : nullptr; }
				VkImageAspectFlags Aspect() const override { return VK_IMAGE_ASPECT_DEPTH_BIT; }
				void* GetNativeHandle() const override    { return const_cast<DepthSRV*>(this); }
				void  Release() override {}
			};

			VkImage       image_  = VK_NULL_HANDLE;
			VmaAllocation alloc_  = VK_NULL_HANDLE;
			VkImageView   arrayView_ = VK_NULL_HANDLE;        // 全スライス 2D_ARRAY (SRV)
			VkImageView   sliceViews_[kArraySize] = {};       // スライス別 2D (DSV attachment)
			DepthSRV      arraySRV_;
			DepthSRV      sliceSRVs_[kArraySize];
			VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
			uint32_t      resolution_ = 0;
			VulkanSampler sampler_;                           // 比較サンプラー (LESS_OR_EQUAL)
		};
	}
}
