#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IGraphicsDeviceImpl.h"
#include <vector>
#include <memory>

struct ImDrawData;

namespace aq
{
	namespace graphics
	{
		class VulkanRenderContextImpl;
		class VulkanPipelineLayout;
		class VulkanPipelineCache;
		class VulkanRenderTarget;

		/**
		 * Vulkan Concrete Implementor (Bridge Pattern) — Phase 0
		 *
		 * D3D12GraphicsDeviceImpl と同じ役割を Vulkan オブジェクトで担う。
		 * Phase 0 は「クリア画面が出る」までを目標とし、リソースファクトリは未実装スタブ。
		 *
		 * 設計: 設計書/VulkanBackend設計.md (§2 フレーム, §8 スワップチェーン)
		 * - Vulkan 1.3 + dynamic rendering (VkRenderPass/Framebuffer を作らない)。
		 * - フレーム境界は最初の記録呼び出しで BeginFrameIfNeeded()、Present() で確定 (D3D12 と同方式)。
		 */
		class VulkanGraphicsDeviceImpl : public IGraphicsDeviceImpl
		{
			friend class VulkanRenderContextImpl;
		public:
			VulkanGraphicsDeviceImpl();
			~VulkanGraphicsDeviceImpl() override;

			bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height) override;
			void Finalize() override;
			void SetupRenderContext(RenderContext& outContext) override;
			uint32_t GetMainRenderTargetCount() const override;
			IRenderTarget& GetMainRenderTarget(uint32_t index) override;
			void Present() override;
			void CopyToBackBuffer(IRenderTarget& src) override;
			void SetupDefaultRenderState(RenderContext& context) override;

			IRenderTarget* GetRenderTarget(uint32_t index) override;
			uint32_t CreateOffscreenRenderTarget(uint32_t width, uint32_t height) override;
			uint32_t CreateOffscreenRenderTarget(const RenderTargetDesc& desc) override;

			// ── リソースファクトリ (Phase 0: 未実装スタブ。Phase 1〜2 で実装) ──
			std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IVertexBuffer>       CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data) override;
			std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) override;
			std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) override;
			std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) override;
			std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) override;
			std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height) override;

			// ── RenderContext / リソース向けアクセサ ──
			VkDevice         GetDevice() const         { return device_; }
			VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }
			VkCommandBuffer  GetCommandBuffer() const  { return frames_[frameIndex_].cmd; }
			VmaAllocator     GetAllocator() const      { return allocator_; }
			uint32_t         GetCurrentFrameIndex() const { return frameIndex_; }
			VkQueue          GetGraphicsQueue() const  { return gfxQueue_; }
			VulkanPipelineLayout* GetPipelineLayout() const { return pipelineLayout_.get(); }
			VulkanPipelineCache*  GetPipelineCache()  const { return pipelineCache_.get(); }
			VkFormat         GetSwapchainFormat() const { return swapchainFormat_; }
			VkExtent2D       GetSwapchainExtent() const { return swapchainExtent_; }
			VkImageView      GetCurrentSwapchainView() const  { return swapchainViews_[imageIndex_]; }
			VkImage          GetCurrentSwapchainImage() const { return swapchainImages_[imageIndex_]; }
			VkDescriptorPool GetCurrentDescriptorPool() const { return frames_[frameIndex_].descPool; }

			// リソースクラス向け静的アクセサ (D3D11/D3D12 層の GetStaticDevice と同じパターン)
			static VmaAllocator GetStaticAllocator();
			static uint32_t     GetStaticFrameIndex();
			static constexpr uint32_t GetFrameCount() { return FRAME_COUNT; }

			/** 最初の記録呼び出しで遅延発火: フレームを開き、バックバッファを COLOR_ATTACHMENT へ遷移する。 */
			void BeginFrameIfNeeded();
			/** メインのスワップチェーン画像を指定色でクリアする (Phase 0: dynamic rendering で begin+clear+end)。 */
			void ClearMainTarget(const float color[4]);

			static VkDevice GetStaticDevice();
			static VulkanGraphicsDeviceImpl* GetInstance();

			// シェーダが静的に使う未バインド SRV/Sampler スロットを埋めるデフォルト (1x1 白 / Clamp)。
			VkImageView GetDefaultTextureView() const;
			VkSampler   GetDefaultSampler() const;

			/** 即時実行コマンド (テクスチャアップロード等)。記録→submit→完了待ちまで同期実行する。 */
			void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& fn);

			/** imgui の描画データを受け取り、CopyToBackBuffer 後に swapchain へ描く (AQ_IMGUI 時)。 */
			void SetImGuiDrawData(ImDrawData* d) { imguiDrawData_ = d; }

		private:
			bool CreateInstance();
			bool CreateSurface(void* hwnd);
			bool PickPhysicalDeviceAndQueues();
			bool CreateLogicalDevice();
			bool CreateAllocator();
			bool CreateSwapchain(uint32_t width, uint32_t height);
			bool CreateFrameResources();
			void TransitionImage(VkCommandBuffer cmd, VkImage image,
			                     VkImageLayout oldLayout, VkImageLayout newLayout,
			                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
			void WaitDeviceIdle();

		private:
			static constexpr uint32_t FRAME_COUNT = 2;

			VkInstance        instance_       = VK_NULL_HANDLE;
			VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;  // _DEBUG 時のみ
			VkSurfaceKHR      surface_        = VK_NULL_HANDLE;
			VkPhysicalDevice  physicalDevice_ = VK_NULL_HANDLE;
			VkDevice          device_         = VK_NULL_HANDLE;
			uint32_t          gfxQueueFamily_ = UINT32_MAX;
			VkQueue           gfxQueue_       = VK_NULL_HANDLE;
			VmaAllocator      allocator_      = VK_NULL_HANDLE;
			VkCommandPool     uploadPool_     = VK_NULL_HANDLE;  // ImmediateSubmit 用 transient プール

			VkSwapchainKHR    swapchain_      = VK_NULL_HANDLE;
			VkFormat          swapchainFormat_ = VK_FORMAT_UNDEFINED;
			VkExtent2D        swapchainExtent_ = { 0, 0 };
			std::vector<VkImage>     swapchainImages_;
			std::vector<VkImageView> swapchainViews_;
			std::vector<VkSemaphore> presentSemaphores_;  // swapchain 画像単位 (present 待ち。再利用安全)
			uint32_t          imageIndex_     = 0;  // vkAcquireNextImageKHR で取得した現在の画像

			struct FrameResources
			{
				VkCommandPool    pool   = VK_NULL_HANDLE;
				VkCommandBuffer  cmd    = VK_NULL_HANDLE;
				VkSemaphore      imageAvailable = VK_NULL_HANDLE;
				VkSemaphore      renderFinished = VK_NULL_HANDLE;
				VkFence          inFlight       = VK_NULL_HANDLE;
				VkDescriptorPool descPool       = VK_NULL_HANDLE;  // フレーム毎に Reset する描画用プール
			};
			FrameResources    frames_[FRAME_COUNT];
			uint32_t          frameIndex_     = 0;
			bool              frameOpen_      = false;

			std::unique_ptr<VulkanPipelineLayout> pipelineLayout_;
			std::unique_ptr<VulkanPipelineCache>  pipelineCache_;
			std::unique_ptr<class VulkanTexture>  defaultTexture_;  // 1x1 白
			std::unique_ptr<class VulkanSampler>  defaultSampler_;  // Clamp/Linear
			std::unique_ptr<VulkanRenderTarget>   mainRTs_[2];    // 深度付きオフスクリーン HDR (D3D12 と同方式・最後に CopyToBackBuffer)
			std::vector<std::unique_ptr<VulkanRenderTarget>> offscreenRTs_;  // GBuffer/ポスプロ等
			VulkanRenderContextImpl*              activeContext_ = nullptr;  // Present 前に rendering を閉じる
			ImDrawData*                          imguiDrawData_ = nullptr;  // CopyToBackBuffer 後に描く

			static constexpr uint32_t MAIN_RT_COUNT = 2;  // RenderTargetHandle のメイン RT 数 (D3D12 と一致)
		};
	}
}
