#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanResources.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <cstring>

namespace aq
{
	namespace graphics
	{
		VkFormat ToVkFormat(PixelFormat fmt)
		{
			switch (fmt)
			{
			case PixelFormat::R8G8B8A8_Unorm:       return VK_FORMAT_R8G8B8A8_UNORM;
			case PixelFormat::R8G8B8A8_Unorm_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
			case PixelFormat::B8G8R8A8_Unorm:       return VK_FORMAT_B8G8R8A8_UNORM;
			case PixelFormat::B8G8R8A8_Unorm_SRGB:  return VK_FORMAT_B8G8R8A8_SRGB;
			case PixelFormat::D24_Unorm_S8_Uint:    return VK_FORMAT_D24_UNORM_S8_UINT;
			case PixelFormat::R16G16B16A16_Float:   return VK_FORMAT_R16G16B16A16_SFLOAT;
			case PixelFormat::R32_Float:            return VK_FORMAT_R32_SFLOAT;
			case PixelFormat::R32G32B32A32_Float:   return VK_FORMAT_R32G32B32A32_SFLOAT;
			case PixelFormat::BC1_Unorm:            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			case PixelFormat::BC1_Unorm_SRGB:       return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
			case PixelFormat::BC2_Unorm:            return VK_FORMAT_BC2_UNORM_BLOCK;
			case PixelFormat::BC2_Unorm_SRGB:       return VK_FORMAT_BC2_SRGB_BLOCK;
			case PixelFormat::BC3_Unorm:            return VK_FORMAT_BC3_UNORM_BLOCK;
			case PixelFormat::BC3_Unorm_SRGB:       return VK_FORMAT_BC3_SRGB_BLOCK;
			case PixelFormat::BC4_Unorm:            return VK_FORMAT_BC4_UNORM_BLOCK;
			case PixelFormat::BC5_Unorm:            return VK_FORMAT_BC5_UNORM_BLOCK;
			case PixelFormat::BC6H_UFloat16:        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			case PixelFormat::BC7_Unorm:            return VK_FORMAT_BC7_UNORM_BLOCK;
			case PixelFormat::BC7_Unorm_SRGB:       return VK_FORMAT_BC7_SRGB_BLOCK;
			default:                                return VK_FORMAT_UNDEFINED;
			}
		}

		namespace
		{
			struct FormatInfo { uint32_t blockW, blockH, blockBytes; bool compressed; };

			FormatInfo GetFormatInfo(PixelFormat fmt)
			{
				switch (fmt)
				{
				case PixelFormat::BC1_Unorm: case PixelFormat::BC1_Unorm_SRGB:
				case PixelFormat::BC4_Unorm:
					return { 4, 4, 8, true };
				case PixelFormat::BC2_Unorm: case PixelFormat::BC2_Unorm_SRGB:
				case PixelFormat::BC3_Unorm: case PixelFormat::BC3_Unorm_SRGB:
				case PixelFormat::BC5_Unorm: case PixelFormat::BC6H_UFloat16:
				case PixelFormat::BC7_Unorm: case PixelFormat::BC7_Unorm_SRGB:
					return { 4, 4, 16, true };
				case PixelFormat::R16G16B16A16_Float:  return { 1, 1, 8,  false };
				case PixelFormat::R32G32B32A32_Float:  return { 1, 1, 16, false };
				case PixelFormat::R32_Float:           return { 1, 1, 4,  false };
				default:                               return { 1, 1, 4,  false };  // 8bit4ch 系
				}
			}

			// (mipW,mipH) の 1 サブリソースの密パッキング行バイト数・行数を返す。
			void TightLayout(const FormatInfo& fi, uint32_t mipW, uint32_t mipH, uint32_t& outRowBytes, uint32_t& outRows)
			{
				const uint32_t bw = (mipW + fi.blockW - 1) / fi.blockW;
				const uint32_t bh = (mipH + fi.blockH - 1) / fi.blockH;
				outRowBytes = bw * fi.blockBytes;
				outRows     = bh;
			}

			VkFilter ToVkFilter(FilterMode m)
			{
				return (m == FilterMode::MinMagMipPoint) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
			}
			VkSamplerMipmapMode ToVkMipMode(FilterMode m)
			{
				return (m == FilterMode::MinMagMipPoint || m == FilterMode::MinMagLinearMipPoint)
					? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
			}
			VkSamplerAddressMode ToVkAddress(AddressMode m)
			{
				switch (m)
				{
				case AddressMode::Clamp:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				case AddressMode::Wrap:   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
				case AddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
				case AddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
				}
				return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			}
		}

		// ── VulkanTexture ──────────────────────────────────────
		bool VulkanTexture::Create(const Texture2DDesc& desc, const ImageData& data)
		{
			Release();
			auto* dev = VulkanGraphicsDeviceImpl::GetInstance();
			if (!dev) return false;
			VkDevice     device    = dev->GetDevice();
			VmaAllocator allocator = dev->GetAllocator();

			const VkFormat format = ToVkFormat(desc.format);
			if (format == VK_FORMAT_UNDEFINED) return false;
			const FormatInfo fi = GetFormatInfo(desc.format);
			const uint32_t mips   = desc.mipLevels ? desc.mipLevels : 1;
			const uint32_t layers = desc.arraySize ? desc.arraySize : 1;

			// 画像本体 (DEVICE_LOCAL, TRANSFER_DST | SAMPLED)
			VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			ici.imageType     = VK_IMAGE_TYPE_2D;
			ici.format        = format;
			ici.extent        = { desc.width, desc.height, 1 };
			ici.mipLevels     = mips;
			ici.arrayLayers   = layers;
			ici.samples       = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
			ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (desc.isCubemap) ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			VmaAllocationCreateInfo aci{};
			aci.usage = VMA_MEMORY_USAGE_AUTO;
			if (!VK_VERIFY(vmaCreateImage(allocator, &ici, &aci, &image_, &alloc_, nullptr))) return false;

			// サブリソース (slice 外・mip 内、index = mip + slice*mips) を密パッキングでステージングへ。
			struct Region { uint32_t mip, layer, w, h, rowBytes, rows; size_t srcOffset; uint32_t srcRowPitch; const void* src; };
			std::vector<Region> regions;
			size_t total = 0;
			const bool hasSub = (data.subresources && data.subresourceCount > 0);
			for (uint32_t s = 0; s < layers; ++s)
			{
				for (uint32_t m = 0; m < mips; ++m)
				{
					const uint32_t mipW = (desc.width  >> m) ? (desc.width  >> m) : 1;
					const uint32_t mipH = (desc.height >> m) ? (desc.height >> m) : 1;
					uint32_t rowBytes, rows;
					TightLayout(fi, mipW, mipH, rowBytes, rows);

					const void* src = nullptr;
					uint32_t srcRowPitch = rowBytes;
					if (hasSub)
					{
						const uint32_t idx = m + s * mips;
						if (idx < data.subresourceCount)
						{
							src         = data.subresources[idx].pixels;
							srcRowPitch = data.subresources[idx].rowPitch ? data.subresources[idx].rowPitch : rowBytes;
						}
					}
					else if (m == 0 && s == 0)
					{
						src         = data.pixels;
						srcRowPitch = data.rowPitch ? data.rowPitch : rowBytes;
					}

					regions.push_back({ m, s, mipW, mipH, rowBytes, rows, total, srcRowPitch, src });
					total += (size_t)rowBytes * rows;
				}
			}
			if (total == 0) { Release(); return false; }

			// ステージングバッファ (HOST_VISIBLE) を作り行単位でコピー (rowPitch→密)
			VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			bci.size  = total;
			bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			VmaAllocationCreateInfo sai{};
			sai.usage = VMA_MEMORY_USAGE_AUTO;
			sai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			VkBuffer staging = VK_NULL_HANDLE; VmaAllocation stagingAlloc = VK_NULL_HANDLE; VmaAllocationInfo si{};
			if (!VK_VERIFY(vmaCreateBuffer(allocator, &bci, &sai, &staging, &stagingAlloc, &si))) { Release(); return false; }
			auto* mapped = static_cast<uint8_t*>(si.pMappedData);

			std::vector<VkBufferImageCopy> copies;
			copies.reserve(regions.size());
			for (const auto& r : regions)
			{
				if (r.src)
				{
					for (uint32_t row = 0; row < r.rows; ++row)
						std::memcpy(mapped + r.srcOffset + (size_t)row * r.rowBytes,
						            static_cast<const uint8_t*>(r.src) + (size_t)row * r.srcRowPitch,
						            r.rowBytes);
				}
				VkBufferImageCopy c{};
				c.bufferOffset      = r.srcOffset;
				c.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, r.mip, r.layer, 1 };
				c.imageExtent       = { r.w, r.h, 1 };
				copies.push_back(c);
			}

			// アップロード: UNDEFINED→TRANSFER_DST→copy→SHADER_READ_ONLY
			VkImage img = image_;
			const uint32_t allMips = mips, allLayers = layers;
			dev->ImmediateSubmit([&](VkCommandBuffer cmd)
			{
				auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
				                   VkPipelineStageFlags2 ss, VkAccessFlags2 sa,
				                   VkPipelineStageFlags2 ds, VkAccessFlags2 da)
				{
					VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
					b.srcStageMask = ss; b.srcAccessMask = sa; b.dstStageMask = ds; b.dstAccessMask = da;
					b.oldLayout = oldL; b.newLayout = newL; b.image = img;
					b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, allMips, 0, allLayers };
					VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
					dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &b;
					vkCmdPipelineBarrier2(cmd, &dep);
				};
				barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
				        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
				vkCmdCopyBufferToImage(cmd, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				                       (uint32_t)copies.size(), copies.data());
				barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			});
			vmaDestroyBuffer(allocator, staging, stagingAlloc);

			// ビュー
			VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			vci.image    = image_;
			vci.viewType = desc.isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE
			             : (layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
			vci.format   = format;
			vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, layers };
			return VK_VERIFY(vkCreateImageView(device, &vci, nullptr, &view_));
		}

		void VulkanTexture::Release()
		{
			auto* dev = VulkanGraphicsDeviceImpl::GetInstance();
			if (!dev) { image_ = VK_NULL_HANDLE; alloc_ = VK_NULL_HANDLE; view_ = VK_NULL_HANDLE; return; }
			VkDevice device = dev->GetDevice();
			if (view_)  { vkDestroyImageView(device, view_, nullptr); view_ = VK_NULL_HANDLE; }
			if (image_) { vmaDestroyImage(dev->GetAllocator(), image_, alloc_); image_ = VK_NULL_HANDLE; alloc_ = VK_NULL_HANDLE; }
		}

		// ── VulkanSampler ──────────────────────────────────────
		bool VulkanSampler::Create(const SamplerDesc& desc)
		{
			Release();
			VkDevice device = VulkanGraphicsDeviceImpl::GetStaticDevice();
			if (!device) return false;

			VkSamplerCreateInfo ci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			ci.magFilter    = ToVkFilter(desc.filter);
			ci.minFilter    = ToVkFilter(desc.filter);
			ci.mipmapMode   = ToVkMipMode(desc.filter);
			ci.addressModeU = ToVkAddress(desc.addressU);
			ci.addressModeV = ToVkAddress(desc.addressV);
			ci.addressModeW = ToVkAddress(desc.addressW);
			ci.mipLodBias   = desc.mipLODBias;
			ci.minLod       = desc.minLOD;
			ci.maxLod       = desc.maxLOD;
			ci.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			if (desc.filter == FilterMode::Anisotropic)
			{
				ci.anisotropyEnable = VK_TRUE;
				ci.maxAnisotropy    = (float)(desc.maxAniso ? desc.maxAniso : 1);
			}
			if (desc.isComparison)
			{
				ci.compareEnable = VK_TRUE;
				ci.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;  // シャドウ SampleCmp 用
			}
			return VK_VERIFY(vkCreateSampler(device, &ci, nullptr, &sampler_));
		}

		void VulkanSampler::Release()
		{
			VkDevice device = VulkanGraphicsDeviceImpl::GetStaticDevice();
			if (sampler_ && device) vkDestroySampler(device, sampler_, nullptr);
			sampler_ = VK_NULL_HANDLE;
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
