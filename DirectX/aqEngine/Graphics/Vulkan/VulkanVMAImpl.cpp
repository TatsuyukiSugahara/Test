// VMA (Vulkan Memory Allocator) の実装本体を生成する専用 TU。
// VMA_IMPLEMENTATION はプロジェクト内で 1 箇所のみ定義すること。
#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanCommon.h"  // vulkan.h (VK_USE_PLATFORM_WIN32_KHR 込み)

#pragma warning(push)
#pragma warning(disable : 4100 4127 4189 4324 4505 4701 4703)  // VMA 内部の良性警告を抑止
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#pragma warning(pop)
#endif // ENGINE_GRAPHICS_VULKAN
