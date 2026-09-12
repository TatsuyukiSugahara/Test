#pragma once
// ── Vulkan 共通ヘッダ ──
// vulkan.h を直接持ち込み、VK_CHECK / 列挙変換ヘルパを提供する。
// PCH(aq.h) からも include されるが、単体 TU でも自己完結するようここで vulkan.h を含める。
#include "Platform/Common/PlatformDefs.h"
#include "Platform/Common/DebugOutput.h"

#if defined(AQ_PLATFORM_WIN32)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#elif defined(AQ_PLATFORM_MAC)
// MoltenVK 上のサーフェスは CAMetalLayer 経由 (VK_EXT_metal_surface)。
#ifndef VK_USE_PLATFORM_METAL_EXT
#define VK_USE_PLATFORM_METAL_EXT
#endif
// VK_KHR_portability_subset は provisional 拡張のため、VK_ENABLE_BETA_EXTENSIONS が無いと
// vulkan_core.h が vulkan_beta.h を取り込まず、VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME /
// VkPhysicalDevicePortabilitySubsetFeaturesKHR /
// VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR が未定義になる。
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#elif defined(AQ_PLATFORM_ANDROID)
// Android のサーフェスは ANativeWindow 経由 (VK_KHR_android_surface)。
// ヘッダと libvulkan.so はどちらも NDK の sysroot が供給するので Vulkan SDK は要らない。
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#endif

#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#pragma warning(push)
#pragma warning(disable : 4005)  // マクロ再定義 (windows.h との衝突回避)
#endif
#include <vulkan/vulkan.h>
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#pragma warning(pop)
#endif

// VMA ハンドルの前方宣言。実体 (vk_mem_alloc.h) は VulkanVMAImpl.cpp / 各 Vulkan .cpp が取り込む。
// VK_DEFINE_HANDLE は同一 typedef の再宣言になるため二重 include でも合法。
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

#include <cstdint>
#include <cstdio>
#include <cassert>

namespace aq
{
	namespace graphics
	{
		// VkResult を確認し、失敗時にログを出して assert する。
		inline bool VkCheckImpl(VkResult r, const char* expr, const char* file, int line)
		{
			if (r != VK_SUCCESS)
			{
				char buf[512];
				std::snprintf(buf, sizeof(buf), "[Vulkan] %s failed (VkResult=%d) at %s:%d\n", expr, (int)r, file, line);
				aq::debug::OutputString(buf);  // Win32/UWP = OutputDebugStringA, Mac = stderr
				std::fprintf(stderr, "%s", buf);
				assert(false && "Vulkan call failed");
				return false;
			}
			return true;
		}
	}
}

// 失敗時 false を返したい箇所で使う (戻り値 bool のスコープ向け)
#define VK_VERIFY(expr) (::aq::graphics::VkCheckImpl((expr), #expr, __FILE__, __LINE__))
// 失敗を無視できない致命箇所 (戻り値不要) 向け。失敗してもログのみで継続。
#define VK_CHECK(expr)  (void)::aq::graphics::VkCheckImpl((expr), #expr, __FILE__, __LINE__)
