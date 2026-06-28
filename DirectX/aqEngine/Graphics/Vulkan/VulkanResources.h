#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/IUnorderedAccessView.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/GraphicsTypes.h"

namespace aq
{
	namespace graphics
	{
		// ── Vulkan 共通 SRV 基底 (Phase 2/3) ──
		// PSSetShaderResource が具象型を問わず VkImageView を取得するための基底。
		// テクスチャ(VulkanTexture)・オフスクリーンRT・深度マップ(Phase 3)の SRV がこれを実装する。
		// 既存コードはテクスチャを DeferredSRV(IShaderResourceView) でラップするため、
		// バインド経路は GetNativeHandle() → static_cast<VulkanSRV*> で実体解決する (D3D12 と同方式)。
		class VulkanSRV : public IShaderResourceView
		{
		public:
			virtual VkImageView GetImageView() const = 0;
			// ── バリア追跡用 (Phase 3) ──
			// オフスクリーン RT/深度を SRV としてサンプルする前に SHADER_READ_ONLY へ遷移するため、
			// 元画像とその現在レイアウト状態へのポインタを公開する。
			// 常時 SHADER_READ_ONLY の通常テクスチャは LayoutPtr()=nullptr (遷移不要)。
			virtual VkImage        GetImage() const  { return VK_NULL_HANDLE; }
			virtual VkImageLayout* LayoutPtr() const  { return nullptr; }
			virtual VkImageAspectFlags Aspect() const { return VK_IMAGE_ASPECT_COLOR_BIT; }
		};


		// ── Vulkan 共通 UAV 基底 (Phase 4 compute) ──
		// compute が RT へ書き込む (RWTexture2D = storage image)。CSSetUnorderedAccessView が
		// storage view を取得し、Dispatch 前に対象画像を GENERAL レイアウトへ遷移する。
		class VulkanUAV : public IUnorderedAccessView
		{
		public:
			virtual VkImageView    GetStorageView() const = 0;
			virtual VkImage        GetImage() const   { return VK_NULL_HANDLE; }
			virtual VkImageLayout* LayoutPtr() const  { return nullptr; }
		};


		// Vulkan テクスチャ2D + SRV (Phase 2)
		class VulkanTexture final : public VulkanSRV
		{
		public:
			VulkanTexture()           = default;
			~VulkanTexture() override { Release(); }

			bool Create(const Texture2DDesc& desc, const ImageData& data);
			void Release() override;

			VkImageView GetImageView() const override { return view_; }
			VkImage     GetImage() const override     { return image_; }  // 常時 SHADER_READ_ONLY (LayoutPtr=nullptr)
			// 未ロード時は nullptr を返し、呼び出し側がスキップできるようにする (D3D12 と同方式)。
			void* GetNativeHandle() const override
			{
				return view_ ? static_cast<VulkanSRV*>(const_cast<VulkanTexture*>(this)) : nullptr;
			}

		private:
			VkImage       image_ = VK_NULL_HANDLE;
			VmaAllocation alloc_ = VK_NULL_HANDLE;
			VkImageView   view_  = VK_NULL_HANDLE;
		};


		// Vulkan サンプラーステート (Phase 2)
		class VulkanSampler final : public ISamplerState
		{
		public:
			VulkanSampler()           = default;
			~VulkanSampler() override { Release(); }

			bool Create(const SamplerDesc& desc) override;
			void Release() override;

			VkSampler GetSampler() const { return sampler_; }

		private:
			VkSampler sampler_ = VK_NULL_HANDLE;
		};


		// PixelFormat → VkFormat (テクスチャ生成・フォーマット問い合わせで共用)
		VkFormat ToVkFormat(PixelFormat fmt);
	}
}
