#include "aq.h"
// 非 Vulkan 構成では Vulkan SDK を要求しないよう本体をガードする (空 TU になる)。
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include "Graphics/Vulkan/VulkanRenderContextImpl.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanBuffers.h"
#include "Graphics/Vulkan/VulkanResources.h"
#include "Graphics/Vulkan/VulkanPipelineLayout.h"
#include "Graphics/Vulkan/VulkanPipelineCache.h"
#include "Graphics/Vulkan/VulkanRenderTarget.h"
#include "Graphics/Vulkan/VulkanDepthMap.h"
#ifdef AQ_IMGUI
#include "Graphics/Vulkan/VulkanImGui.h"
#endif
#include "Graphics/RenderContext.h"
#include <vma/vk_mem_alloc.h>

namespace aq
{
	namespace graphics
	{
		namespace
		{
			VulkanGraphicsDeviceImpl* g_staticDevice = nullptr;
		}

		VulkanGraphicsDeviceImpl::VulkanGraphicsDeviceImpl()
		{
			g_staticDevice = this;
		}

		VulkanGraphicsDeviceImpl::~VulkanGraphicsDeviceImpl()
		{
			Finalize();
			if (g_staticDevice == this) g_staticDevice = nullptr;
		}

		VkDevice VulkanGraphicsDeviceImpl::GetStaticDevice()
		{
			return g_staticDevice ? g_staticDevice->device_ : VK_NULL_HANDLE;
		}

		VulkanGraphicsDeviceImpl* VulkanGraphicsDeviceImpl::GetInstance()
		{
			return g_staticDevice;
		}

		void VulkanGraphicsDeviceImpl::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& fn)
		{
			VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			ai.commandPool        = uploadPool_;
			ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			ai.commandBufferCount = 1;
			VkCommandBuffer cmd = VK_NULL_HANDLE;
			if (!VK_VERIFY(vkAllocateCommandBuffers(device_, &ai, &cmd))) return;

			VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cmd, &bi);
			fn(cmd);
			vkEndCommandBuffer(cmd);

			VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			cmdInfo.commandBuffer = cmd;
			VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
			submit.commandBufferInfoCount = 1;
			submit.pCommandBufferInfos    = &cmdInfo;
			vkQueueSubmit2(gfxQueue_, 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(gfxQueue_);  // 同期実行 (アップロードは初期化フェーズが主)

			vkFreeCommandBuffers(device_, uploadPool_, 1, &cmd);
		}

		VmaAllocator VulkanGraphicsDeviceImpl::GetStaticAllocator()
		{
			return g_staticDevice ? g_staticDevice->allocator_ : VK_NULL_HANDLE;
		}

		uint32_t VulkanGraphicsDeviceImpl::GetStaticFrameIndex()
		{
			return g_staticDevice ? g_staticDevice->frameIndex_ : 0;
		}

		// ── 初期化 ──────────────────────────────────────────────
		bool VulkanGraphicsDeviceImpl::Initialize(NativeWindowHandle window, uint32_t width, uint32_t height)
		{
			if (!CreateInstance())                 return false;
			if (!CreateSurface(window.handle))     return false;
			if (!PickPhysicalDeviceAndQueues())    return false;
			if (!CreateLogicalDevice())            return false;
			if (!CreateAllocator())                return false;
			if (!CreateSwapchain(width, height))   return false;
			if (!CreateFrameResources())           return false;

			pipelineLayout_ = std::make_unique<VulkanPipelineLayout>();
			if (!pipelineLayout_->Create(device_)) return false;
			pipelineCache_ = std::make_unique<VulkanPipelineCache>();

			// メイン RT は深度付きオフスクリーン HDR (R16G16B16A16_FLOAT)。D3D12 と同方式で、
			// 最後に CopyToBackBuffer でバックバッファへブリットする (HDR→LDR)。
			for (auto& rt : mainRTs_)
			{
				rt = std::make_unique<VulkanRenderTarget>();
				if (!rt->CreateOffscreen(swapchainExtent_.width, swapchainExtent_.height,
				                         VK_FORMAT_R16G16B16A16_SFLOAT, /*hasDepth*/true))
					return false;
			}

			// シェーダが静的参照する未バインド SRV/Sampler を埋めるデフォルト (1x1 白)。
			{
				const uint32_t white = 0xFFFFFFFFu;
				Texture2DDesc td; td.width = 1; td.height = 1; td.format = PixelFormat::R8G8B8A8_Unorm;
				ImageData id; id.pixels = &white; id.rowPitch = 4; id.slicePitch = 4;
				defaultTexture_ = std::make_unique<VulkanTexture>();
				defaultTexture_->Create(td, id);
				defaultSampler_ = std::make_unique<VulkanSampler>();
				defaultSampler_->Create(SamplerDesc{});
			}
			return true;
		}

		VkImageView VulkanGraphicsDeviceImpl::GetDefaultTextureView() const
		{
			return defaultTexture_ ? defaultTexture_->GetImageView() : VK_NULL_HANDLE;
		}
		VkSampler VulkanGraphicsDeviceImpl::GetDefaultSampler() const
		{
			return defaultSampler_ ? defaultSampler_->GetSampler() : VK_NULL_HANDLE;
		}

#ifdef _DEBUG
		namespace
		{
			// 検証レイヤ出力をログファイル + OutputDebugString へ。ウィンドウアプリで stderr が
			// 繋がらないため、CWD の vk_debug.log に追記する (実機検証で参照)。
			VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugCallback(
				VkDebugUtilsMessageSeverityFlagBitsEXT severity,
				VkDebugUtilsMessageTypeFlagsEXT /*type*/,
				const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
			{
				const char* sev = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR"
				                : (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARN" : "INFO";
				char line[2048];
				std::snprintf(line, sizeof(line), "[VK %s] %s\n", sev, data->pMessage ? data->pMessage : "");
				OutputDebugStringA(line);
				if (FILE* fp = nullptr; fopen_s(&fp, "vk_debug.log", "a") == 0 && fp)
				{
					std::fputs(line, fp);
					std::fclose(fp);
				}
				return VK_FALSE;
			}

			bool HasValidationLayer()
			{
				uint32_t count = 0;
				vkEnumerateInstanceLayerProperties(&count, nullptr);
				std::vector<VkLayerProperties> layers(count);
				vkEnumerateInstanceLayerProperties(&count, layers.data());
				for (const auto& l : layers)
					if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) return true;
				return false;
			}
		}
#endif

		bool VulkanGraphicsDeviceImpl::CreateInstance()
		{
			VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			app.pApplicationName = "aqEngine";
			app.apiVersion       = VK_API_VERSION_1_3;

			std::vector<const char*> exts = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
			std::vector<const char*> layers;

			VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			ci.pApplicationInfo = &app;
#ifdef _DEBUG
			const bool validation = HasValidationLayer();
			if (validation)
			{
				layers.push_back("VK_LAYER_KHRONOS_validation");
				exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				std::remove("vk_debug.log");  // 起動毎にリセット
			}
#endif
			ci.enabledExtensionCount   = (uint32_t)exts.size();
			ci.ppEnabledExtensionNames = exts.data();
			ci.enabledLayerCount       = (uint32_t)layers.size();
			ci.ppEnabledLayerNames     = layers.empty() ? nullptr : layers.data();
			if (!VK_VERIFY(vkCreateInstance(&ci, nullptr, &instance_))) return false;

#ifdef _DEBUG
			if (validation)
			{
				auto create = (PFN_vkCreateDebugUtilsMessengerEXT)
					vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
				if (create)
				{
					VkDebugUtilsMessengerCreateInfoEXT dci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
					dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
					dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
					dci.pfnUserCallback = VkDebugCallback;
					create(instance_, &dci, nullptr, &debugMessenger_);
				}
			}
#endif
			return true;
		}

		bool VulkanGraphicsDeviceImpl::CreateSurface(void* hwnd)
		{
			VkWin32SurfaceCreateInfoKHR ci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
			ci.hinstance = GetModuleHandle(nullptr);
			ci.hwnd      = (HWND)hwnd;
			return VK_VERIFY(vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_));
		}

		bool VulkanGraphicsDeviceImpl::PickPhysicalDeviceAndQueues()
		{
			uint32_t count = 0;
			vkEnumeratePhysicalDevices(instance_, &count, nullptr);
			if (count == 0) return false;
			std::vector<VkPhysicalDevice> devices(count);
			vkEnumeratePhysicalDevices(instance_, &count, devices.data());

			// 離散 GPU を優先。グラフィクス+present 対応のキューファミリを持つ最初のデバイスを選ぶ。
			VkPhysicalDevice fallback = VK_NULL_HANDLE;
			for (VkPhysicalDevice pd : devices)
			{
				uint32_t qCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
				std::vector<VkQueueFamilyProperties> props(qCount);
				vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, props.data());

				for (uint32_t i = 0; i < qCount; ++i)
				{
					VkBool32 present = VK_FALSE;
					vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
					if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
					{
						VkPhysicalDeviceProperties dp;
						vkGetPhysicalDeviceProperties(pd, &dp);
						if (dp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
						{
							physicalDevice_ = pd;
							gfxQueueFamily_ = i;
							return true;
						}
						if (fallback == VK_NULL_HANDLE)
						{
							fallback        = pd;
							gfxQueueFamily_ = i;
						}
					}
				}
			}
			physicalDevice_ = fallback;
			return physicalDevice_ != VK_NULL_HANDLE;
		}

		bool VulkanGraphicsDeviceImpl::CreateLogicalDevice()
		{
			float priority = 1.0f;
			VkDeviceQueueCreateInfo q{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			q.queueFamilyIndex = gfxQueueFamily_;
			q.queueCount       = 1;
			q.pQueuePriorities = &priority;

			// dynamic rendering / synchronization2 を有効化 (Vulkan 1.3 コア機能)。
			VkPhysicalDeviceVulkan13Features feat13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			feat13.dynamicRendering = VK_TRUE;
			feat13.synchronization2 = VK_TRUE;
			// scalarBlockLayout: クラスタカリング等の compute SSBO レイアウトを許容 (Phase 4)。
			VkPhysicalDeviceVulkan12Features feat12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			feat12.scalarBlockLayout = VK_TRUE;
			feat13.pNext = &feat12;

			// 基本機能: 異方性サンプリング (Anisotropic サンプラ用)。
			VkPhysicalDeviceFeatures baseFeatures{};
			baseFeatures.samplerAnisotropy = VK_TRUE;

			const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

			VkDeviceCreateInfo ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			ci.pNext                   = &feat13;
			ci.queueCreateInfoCount    = 1;
			ci.pQueueCreateInfos       = &q;
			ci.enabledExtensionCount   = (uint32_t)std::size(devExts);
			ci.ppEnabledExtensionNames = devExts;
			ci.pEnabledFeatures        = &baseFeatures;
			if (!VK_VERIFY(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_))) return false;

			vkGetDeviceQueue(device_, gfxQueueFamily_, 0, &gfxQueue_);
			return true;
		}

		bool VulkanGraphicsDeviceImpl::CreateAllocator()
		{
			VmaAllocatorCreateInfo ci{};
			ci.physicalDevice   = physicalDevice_;
			ci.device           = device_;
			ci.instance         = instance_;
			ci.vulkanApiVersion = VK_API_VERSION_1_3;
			if (!VK_VERIFY(vmaCreateAllocator(&ci, &allocator_))) return false;

			VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			pci.queueFamilyIndex = gfxQueueFamily_;
			return VK_VERIFY(vkCreateCommandPool(device_, &pci, nullptr, &uploadPool_));
		}

		bool VulkanGraphicsDeviceImpl::CreateSwapchain(uint32_t width, uint32_t height)
		{
			VkSurfaceCapabilitiesKHR caps{};
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

			// フォーマット選択: B8G8R8A8_UNORM (sRGB ではなく UNORM。トーンマップ済み出力を想定)。
			uint32_t fmtCount = 0;
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(fmtCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, formats.data());
			VkSurfaceFormatKHR chosen = formats[0];
			for (const auto& f : formats)
			{
				if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					chosen = f;
					break;
				}
			}
			swapchainFormat_ = chosen.format;

			swapchainExtent_ = caps.currentExtent.width != UINT32_MAX
				? caps.currentExtent
				: VkExtent2D{ width, height };

			uint32_t imageCount = caps.minImageCount + 1;
			if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

			VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
			ci.surface          = surface_;
			ci.minImageCount    = imageCount;
			ci.imageFormat      = chosen.format;
			ci.imageColorSpace  = chosen.colorSpace;
			ci.imageExtent      = swapchainExtent_;
			ci.imageArrayLayers = 1;
			ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ci.preTransform     = caps.currentTransform;
			ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;  // VSync。常に対応。
			ci.clipped          = VK_TRUE;
			if (!VK_VERIFY(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_))) return false;

			uint32_t scCount = 0;
			vkGetSwapchainImagesKHR(device_, swapchain_, &scCount, nullptr);
			swapchainImages_.resize(scCount);
			vkGetSwapchainImagesKHR(device_, swapchain_, &scCount, swapchainImages_.data());

			swapchainViews_.resize(scCount);
			presentSemaphores_.resize(scCount);
			for (uint32_t i = 0; i < scCount; ++i)
			{
				VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				vi.image            = swapchainImages_[i];
				vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
				vi.format           = swapchainFormat_;
				vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				if (!VK_VERIFY(vkCreateImageView(device_, &vi, nullptr, &swapchainViews_[i]))) return false;

				// present 待ちセマフォは swapchain 画像単位 (frame 単位だと再利用が安全でない)。
				VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
				if (!VK_VERIFY(vkCreateSemaphore(device_, &si, nullptr, &presentSemaphores_[i]))) return false;
			}
			return true;
		}

		bool VulkanGraphicsDeviceImpl::CreateFrameResources()
		{
			for (uint32_t i = 0; i < FRAME_COUNT; ++i)
			{
				VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
				pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				pci.queueFamilyIndex = gfxQueueFamily_;
				if (!VK_VERIFY(vkCreateCommandPool(device_, &pci, nullptr, &frames_[i].pool))) return false;

				VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
				ai.commandPool        = frames_[i].pool;
				ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				ai.commandBufferCount = 1;
				if (!VK_VERIFY(vkAllocateCommandBuffers(device_, &ai, &frames_[i].cmd))) return false;

				VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
				if (!VK_VERIFY(vkCreateSemaphore(device_, &si, nullptr, &frames_[i].imageAvailable))) return false;
				if (!VK_VERIFY(vkCreateSemaphore(device_, &si, nullptr, &frames_[i].renderFinished))) return false;

				VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
				fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 初回 BeginFrame で即 wait を抜けられるように。
				if (!VK_VERIFY(vkCreateFence(device_, &fi, nullptr, &frames_[i].inFlight))) return false;

				// 描画用ディスクリプタプール (フレーム毎に Reset)。UBO/SampledImage/Sampler を多めに確保。
				VkDescriptorPoolSize sizes[] = {
					{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192 },
					{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  8192 },
					{ VK_DESCRIPTOR_TYPE_SAMPLER,        2048 },
					{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2048 },
				};
				VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
				dpci.maxSets       = 4096;
				dpci.poolSizeCount = (uint32_t)std::size(sizes);
				dpci.pPoolSizes    = sizes;
				if (!VK_VERIFY(vkCreateDescriptorPool(device_, &dpci, nullptr, &frames_[i].descPool))) return false;
			}
			return true;
		}

		// ── フレーム ────────────────────────────────────────────
		void VulkanGraphicsDeviceImpl::BeginFrameIfNeeded()
		{
			if (frameOpen_) return;
			FrameResources& f = frames_[frameIndex_];

			vkWaitForFences(device_, 1, &f.inFlight, VK_TRUE, UINT64_MAX);
			vkResetFences(device_, 1, &f.inFlight);

			vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &imageIndex_);

			vkResetCommandBuffer(f.cmd, 0);
			if (f.descPool) vkResetDescriptorPool(device_, f.descPool, 0);
			VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(f.cmd, &bi);

			// swapchain を COLOR_ATTACHMENT へ (最終的に CopyToBackBuffer がブリットで全面を埋める)。
			TransitionImage(f.cmd, swapchainImages_[imageIndex_],
			                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
			                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
			frameOpen_ = true;
		}

		void VulkanGraphicsDeviceImpl::ClearMainTarget(const float color[4])
		{
			BeginFrameIfNeeded();
			FrameResources& f = frames_[frameIndex_];

			VkRenderingAttachmentInfo att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			att.imageView   = swapchainViews_[imageIndex_];
			att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
			att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
			att.clearValue.color = { { color[0], color[1], color[2], color[3] } };

			VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
			ri.renderArea           = { { 0, 0 }, swapchainExtent_ };
			ri.layerCount           = 1;
			ri.colorAttachmentCount = 1;
			ri.pColorAttachments    = &att;

			vkCmdBeginRendering(f.cmd, &ri);
			vkCmdEndRendering(f.cmd);
		}

		void VulkanGraphicsDeviceImpl::Present()
		{
			BeginFrameIfNeeded();
			FrameResources& f = frames_[frameIndex_];

			// RenderContext が dynamic rendering を開いたままなら閉じる。
			if (activeContext_) activeContext_->EndRenderingIfActive();

			// COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
			TransitionImage(f.cmd, swapchainImages_[imageIndex_],
			                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

			vkEndCommandBuffer(f.cmd);

			VkSemaphoreSubmitInfo waitSem{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			waitSem.semaphore = f.imageAvailable;
			waitSem.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

			VkSemaphore presentSem = presentSemaphores_[imageIndex_];  // 画像単位 (再利用安全)
			VkSemaphoreSubmitInfo signalSem{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			signalSem.semaphore = presentSem;
			signalSem.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

			VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			cmdInfo.commandBuffer = f.cmd;

			VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
			submit.waitSemaphoreInfoCount   = 1;
			submit.pWaitSemaphoreInfos      = &waitSem;
			submit.commandBufferInfoCount   = 1;
			submit.pCommandBufferInfos      = &cmdInfo;
			submit.signalSemaphoreInfoCount = 1;
			submit.pSignalSemaphoreInfos    = &signalSem;
			vkQueueSubmit2(gfxQueue_, 1, &submit, f.inFlight);

			VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			present.waitSemaphoreCount = 1;
			present.pWaitSemaphores    = &presentSem;
			present.swapchainCount     = 1;
			present.pSwapchains        = &swapchain_;
			present.pImageIndices      = &imageIndex_;
			vkQueuePresentKHR(gfxQueue_, &present);

			frameOpen_ = false;
			frameIndex_ = (frameIndex_ + 1) % FRAME_COUNT;
		}

		void VulkanGraphicsDeviceImpl::TransitionImage(VkCommandBuffer cmd, VkImage image,
		                                               VkImageLayout oldLayout, VkImageLayout newLayout,
		                                               VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		                                               VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
		{
			VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			barrier.srcStageMask  = srcStage;
			barrier.srcAccessMask = srcAccess;
			barrier.dstStageMask  = dstStage;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout     = oldLayout;
			barrier.newLayout     = newLayout;
			barrier.image         = image;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

			VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dep.imageMemoryBarrierCount = 1;
			dep.pImageMemoryBarriers    = &barrier;
			vkCmdPipelineBarrier2(cmd, &dep);
		}

		void VulkanGraphicsDeviceImpl::WaitDeviceIdle()
		{
			if (device_) vkDeviceWaitIdle(device_);
		}

		// ── 終了 ────────────────────────────────────────────────
		void VulkanGraphicsDeviceImpl::Finalize()
		{
			if (!device_) return;
			WaitDeviceIdle();

			defaultTexture_.reset();
			defaultSampler_.reset();
			offscreenRTs_.clear();
			for (auto& rt : mainRTs_) rt.reset();
			activeContext_ = nullptr;
			if (pipelineCache_)  { pipelineCache_->Destroy(device_);  pipelineCache_.reset(); }
			if (pipelineLayout_) { pipelineLayout_->Destroy(device_); pipelineLayout_.reset(); }

			for (auto& f : frames_)
			{
				if (f.descPool)       vkDestroyDescriptorPool(device_, f.descPool, nullptr);
				if (f.inFlight)       vkDestroyFence(device_, f.inFlight, nullptr);
				if (f.imageAvailable) vkDestroySemaphore(device_, f.imageAvailable, nullptr);
				if (f.renderFinished) vkDestroySemaphore(device_, f.renderFinished, nullptr);
				if (f.pool)           vkDestroyCommandPool(device_, f.pool, nullptr);
				f = {};
			}
			for (VkSemaphore s : presentSemaphores_) vkDestroySemaphore(device_, s, nullptr);
			presentSemaphores_.clear();
			for (VkImageView v : swapchainViews_) vkDestroyImageView(device_, v, nullptr);
			swapchainViews_.clear();
			swapchainImages_.clear();
			if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
			if (uploadPool_) { vkDestroyCommandPool(device_, uploadPool_, nullptr); uploadPool_ = VK_NULL_HANDLE; }
			if (allocator_) { vmaDestroyAllocator(allocator_); allocator_ = VK_NULL_HANDLE; }
			vkDestroyDevice(device_, nullptr);
			device_ = VK_NULL_HANDLE;
			if (surface_)  { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
#ifdef _DEBUG
			if (debugMessenger_)
			{
				auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)
					vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
				if (destroy) destroy(instance_, debugMessenger_, nullptr);
				debugMessenger_ = VK_NULL_HANDLE;
			}
#endif
			if (instance_) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
		}

		// ── RenderContext ───────────────────────────────────────
		void VulkanGraphicsDeviceImpl::SetupRenderContext(RenderContext& outContext)
		{
			auto impl = std::make_unique<VulkanRenderContextImpl>(this);
			activeContext_ = impl.get();
			outContext.SetImpl(std::move(impl));
		}

		void VulkanGraphicsDeviceImpl::SetupDefaultRenderState(RenderContext& /*context*/)
		{
			// Phase 0: 既定レンダーステートは Pipeline 生成時 (Phase 1b) に確定するため何もしない。
		}

		// ── レンダーターゲット (Phase 0: メイン=スワップチェーンのみ。オフスクリーンは Phase 3) ──
		uint32_t VulkanGraphicsDeviceImpl::GetMainRenderTargetCount() const
		{
			return MAIN_RT_COUNT;
		}

		IRenderTarget& VulkanGraphicsDeviceImpl::GetMainRenderTarget(uint32_t index)
		{
			if (index >= MAIN_RT_COUNT) index = 0;
			return *mainRTs_[index];
		}

		IRenderTarget* VulkanGraphicsDeviceImpl::GetRenderTarget(uint32_t index)
		{
			// index < MAIN_RT_COUNT はメイン (swapchain proxy)、それ以上はオフスクリーン。
			if (index < MAIN_RT_COUNT) return mainRTs_[index].get();
			const uint32_t off = index - MAIN_RT_COUNT;
			if (off < offscreenRTs_.size()) return offscreenRTs_[off].get();
			return nullptr;
		}

		uint32_t VulkanGraphicsDeviceImpl::CreateOffscreenRenderTarget(uint32_t width, uint32_t height)
		{
			RenderTargetDesc desc;
			desc.width  = width;
			desc.height = height;
			return CreateOffscreenRenderTarget(desc);
		}

		uint32_t VulkanGraphicsDeviceImpl::CreateOffscreenRenderTarget(const RenderTargetDesc& desc)
		{
			auto rt = std::make_unique<VulkanRenderTarget>();
			const VkFormat fmt = ToVkFormat(desc.colorFormat);
			if (!rt->CreateOffscreen(desc.width, desc.height,
			                         fmt != VK_FORMAT_UNDEFINED ? fmt : VK_FORMAT_R8G8B8A8_UNORM,
			                         desc.hasDepth))
				return ~0u;
			offscreenRTs_.push_back(std::move(rt));
			return MAIN_RT_COUNT + (uint32_t)(offscreenRTs_.size() - 1);
		}

		void VulkanGraphicsDeviceImpl::CopyToBackBuffer(IRenderTarget& src)
		{
			BeginFrameIfNeeded();
			auto& rt = static_cast<VulkanRenderTarget&>(src);
			if (activeContext_) activeContext_->EndRenderingIfActive();
			VkCommandBuffer cmd = frames_[frameIndex_].cmd;
			VkImage srcImg = rt.GetImage();
			VkImage dstImg = swapchainImages_[imageIndex_];
			if (!srcImg || rt.IsProxy()) return;

			// src(オフスクリーン) → TRANSFER_SRC、backbuffer → TRANSFER_DST
			VkImageLayout* srcLayout = rt.ColorLayoutPtr();
			TransitionImage(cmd, srcImg, *srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
			                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
			*srcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			// backbuffer は BeginFrame で COLOR_ATTACHMENT。TRANSFER_DST へ。
			TransitionImage(cmd, dstImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

			VkExtent2D ext = rt.GetExtent();
			VkImageBlit2 blit{ VK_STRUCTURE_TYPE_IMAGE_BLIT_2 };
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			blit.srcOffsets[1]  = { (int32_t)ext.width, (int32_t)ext.height, 1 };
			blit.dstOffsets[1]  = { (int32_t)swapchainExtent_.width, (int32_t)swapchainExtent_.height, 1 };
			VkBlitImageInfo2 bi{ VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2 };
			bi.srcImage = srcImg; bi.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			bi.dstImage = dstImg; bi.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			bi.regionCount = 1; bi.pRegions = &blit;
			bi.filter = VK_FILTER_LINEAR;
			vkCmdBlitImage2(cmd, &bi);

			// backbuffer を PRESENT 直前状態 (COLOR_ATTACHMENT) に戻す → Present の遷移に繋ぐ。
			TransitionImage(cmd, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

#ifdef AQ_IMGUI
			// imgui をブリット後の swapchain (COLOR_ATTACHMENT) へ直接描画する。
			if (imguiDrawData_)
			{
				VkRenderingAttachmentInfo att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				att.imageView   = swapchainViews_[imageIndex_];
				att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				att.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
				att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
				VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
				ri.renderArea           = { { 0, 0 }, swapchainExtent_ };
				ri.layerCount           = 1;
				ri.colorAttachmentCount = 1;
				ri.pColorAttachments    = &att;
				vkCmdBeginRendering(cmd, &ri);
				VulkanImGui::Render(cmd, imguiDrawData_);
				vkCmdEndRendering(cmd);
			}
#endif
		}

		// ── リソースファクトリ (Phase 1a: バッファ/シェーダ実装。テクスチャ/サンプラ/深度は Phase 2/3) ──
		std::unique_ptr<IVertexBuffer> VulkanGraphicsDeviceImpl::CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vb = std::make_unique<VulkanVertexBuffer>();
			if (!vb->Create(vertexNum, stride, data)) return nullptr;
			return vb;
		}
		std::unique_ptr<IVertexBuffer> VulkanGraphicsDeviceImpl::CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vb = std::make_unique<VulkanVertexBuffer>();
			if (!vb->CreateDynamic(vertexNum, stride, data)) return nullptr;
			return vb;
		}
		std::unique_ptr<IIndexBuffer> VulkanGraphicsDeviceImpl::CreateIndexBuffer(uint32_t indexNum, const void* data)
		{
			auto ib = std::make_unique<VulkanIndexBuffer>();
			if (!ib->Create(indexNum, data)) return nullptr;
			return ib;
		}
		std::unique_ptr<IIndexBuffer> VulkanGraphicsDeviceImpl::CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data)
		{
			auto ib = std::make_unique<VulkanIndexBuffer>();
			if (!ib->CreateDynamic(indexNum, format, data)) return nullptr;
			return ib;
		}
		std::unique_ptr<IConstantBuffer> VulkanGraphicsDeviceImpl::CreateConstantBuffer(const void* data, uint32_t size)
		{
			auto cb = std::make_unique<VulkanConstantBuffer>();
			if (!cb->Create(data, size)) return nullptr;
			return cb;
		}
		std::unique_ptr<IShader> VulkanGraphicsDeviceImpl::CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type)
		{
			auto sh = std::make_unique<VulkanShader>();
			if (!sh->Load(filePath, entryFunc, type)) return nullptr;
			return sh;
		}
		std::unique_ptr<ISamplerState> VulkanGraphicsDeviceImpl::CreateSamplerState(const SamplerDesc& desc)
		{
			auto s = std::make_unique<VulkanSampler>();
			if (!s->Create(desc)) return nullptr;
			return s;
		}
		std::unique_ptr<IShaderResourceView> VulkanGraphicsDeviceImpl::CreateTexture2D(const Texture2DDesc& desc, const ImageData& data)
		{
			auto t = std::make_unique<VulkanTexture>();
			if (!t->Create(desc, data)) return nullptr;
			return t;
		}
		std::unique_ptr<IDepthMap> VulkanGraphicsDeviceImpl::CreateDepthMap(uint32_t width, uint32_t /*height*/)
		{
			// シャドウ用は正方形 (resolution=width)。
			auto dm = std::make_unique<VulkanDepthMap>();
			if (!dm->Create(width)) return nullptr;
			return dm;
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
