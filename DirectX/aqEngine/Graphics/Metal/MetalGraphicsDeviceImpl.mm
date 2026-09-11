#include "aq.h"
// 非 Metal 構成では本体をガードして空 TU にする(Vulkan バックエンドと同じ作法)。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/Metal/MetalRenderContextImpl.h"
#include "Graphics/Metal/MetalRenderTarget.h"
#include "Graphics/Metal/MetalDepthMap.h"
#include "Graphics/Metal/MetalBuffers.h"
#include "Graphics/Metal/MetalResources.h"
#include "Graphics/Metal/MetalShader.h"
#include "Graphics/RenderContext.h"

// 本 TU は手動参照カウント(MRR)前提。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalGraphicsDeviceImpl.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		/**
		 * Metal オブジェクト群。
		 * 素の C++ ヘッダへ Objective-C 型を出さないため、定義をこの TU に閉じ込める
		 * (PlatformMac.mm の MacWindowObjects と同じ手。設計書/MetalBackend設計.md §10)。
		 * MRR なので new... / retain した分は Finalize() で必ず release する。
		 */
		struct MetalDeviceObjects
		{
			id<MTLDevice>        device            = nil;
			id<MTLCommandQueue>  commandQueue      = nil;

			/** PlatformMac が生成したスワップチェーンのレイヤ(こちらでも retain して保持) */
			CAMetalLayer*        layer             = nil;

			/** 直近にコミットしたコマンドバッファ。WaitIdle() の待機対象(設計書 §2.2) */
			id<MTLCommandBuffer> lastCommandBuffer = nil;
		};


		namespace
		{
			/** 生成済みデバイス。リソースクラスからの静的アクセス用(Vulkan / D3D12 と同じ作法) */
			MetalGraphicsDeviceImpl* g_staticDevice = nullptr;

			/** CreateOffscreenRenderTarget の失敗値(IGraphicsDeviceImpl の契約) */
			static constexpr uint32_t INVALID_RT_INDEX = ~0u;

			/** 素の C++ ヘッダ越しに持っている MTLDevice を取り出す */
			inline id<MTLDevice> ToMTLDevice(const MetalGraphicsDeviceImpl* impl)
			{
				return (impl != nullptr) ? static_cast<id<MTLDevice>>(impl->GetMTLDeviceHandle()) : nil;
			}
		}


		MetalGraphicsDeviceImpl::MetalGraphicsDeviceImpl()
			: objects_(nullptr)
			, width_(0)
			, height_(0)
			, offscreenRTs_()
			, activeContext_(nullptr)
		{
			g_staticDevice = this;
		}


		MetalGraphicsDeviceImpl::~MetalGraphicsDeviceImpl()
		{
			Finalize();
			if (g_staticDevice == this) {
				g_staticDevice = nullptr;
			}
		}


		MetalGraphicsDeviceImpl* MetalGraphicsDeviceImpl::GetInstance()
		{
			return g_staticDevice;
		}


		/**
		 * 初期化 / 終了
		 */
		bool MetalGraphicsDeviceImpl::Initialize(NativeWindowHandle window, uint32_t width, uint32_t height)
		{
			EngineAssert(width);
			EngineAssert(height);

			width_   = width;
			height_  = height;
			objects_ = new MetalDeviceObjects();

			// デバイスとコマンドキュー
			@autoreleasepool
			{
				// MTLCreateSystemDefaultDevice は +1 で返る(NS_RETURNS_RETAINED)。
				objects_->device = MTLCreateSystemDefaultDevice();
				if (objects_->device == nil) {
					aq::StartupMark("  [metal] MTLCreateSystemDefaultDevice failed");
					return false;
				}
				aq::StartupMarkf("  [metal] device ok (%s)", [[objects_->device name] UTF8String]);

				objects_->commandQueue = [objects_->device newCommandQueue];  // MRR: +1
				if (objects_->commandQueue == nil) {
					aq::StartupMark("  [metal] newCommandQueue failed");
					return false;
				}
				aq::StartupMark("  [metal] command queue ok");
			}

			// スワップチェーン(CAMetalLayer)。PlatformMac が生成済みのものを設定するだけ(設計書 §7)。
			@autoreleasepool
			{
				CAMetalLayer* layer = static_cast<CAMetalLayer*>(window.handle);
				if (layer == nil || ![layer isKindOfClass:[CAMetalLayer class]]) {
					aq::StartupMark("  [metal] NativeWindowHandle is not a CAMetalLayer");
					return false;
				}

				objects_->layer = [layer retain];
				[layer setDevice:objects_->device];
				[layer setPixelFormat:metal::SWAPCHAIN_PIXEL_FORMAT];
				// CopyToBackBuffer(blit)の宛先に使うため framebufferOnly の最適化は落とす。
				[layer setFramebufferOnly:NO];
				// P0 は何も描かないので、ウィンドウが黒く見えるよう不透明にしておく。
				[layer setOpaque:YES];
				// contentsScale は触らない。PlatformMac が 1 に固定済み(設計書/Mac移植設計.md §8-13)。
				aq::StartupMark("  [metal] CAMetalLayer configured");
			}

			// メイン RT。RenderTargetHandle と ToggleMainRenderTarget が 2 枚前提(設計書 §7)。
			{
				for (auto& renderTarget : mainRTs_)
				{
					renderTarget = std::make_unique<MetalRenderTarget>();
					if (!renderTarget->CreateOffscreen(objects_->device, width_, height_,
					                                   metal::SWAPCHAIN_PIXEL_FORMAT, /*hasDepth*/true)) {
						aq::StartupMark("  [metal] main render target creation failed");
						return false;
					}
				}
				aq::StartupMarkf("  [metal] main render targets ok (x%u, %ux%u)", MAIN_RT_COUNT, width_, height_);
			}
			return true;
		}


		void MetalGraphicsDeviceImpl::Finalize()
		{
			if (objects_ == nullptr) {
				return;
			}

			// GPU が参照中かもしれないリソースを壊さないよう、先にアイドル化する(設計書 §2.2)。
			WaitIdle();

			// 生成と逆順に解放する。
			offscreenRTs_.clear();
			for (auto& renderTarget : mainRTs_) {
				renderTarget.reset();
			}
			activeContext_ = nullptr;

			@autoreleasepool
			{
				[objects_->lastCommandBuffer release];
				objects_->lastCommandBuffer = nil;

				// レイヤの所有者は PlatformMac。こちらの retain 分だけを返す。
				[objects_->layer release];
				objects_->layer = nil;

				[objects_->commandQueue release];
				objects_->commandQueue = nil;

				[objects_->device release];
				objects_->device = nil;
			}

			delete objects_;
			objects_ = nullptr;
		}


		void MetalGraphicsDeviceImpl::WaitIdle()
		{
			if (objects_ == nullptr || objects_->lastCommandBuffer == nil) {
				return;
			}

			// Metal には vkDeviceWaitIdle 相当が無い。同一キューは投入順に完了するので、
			// 直近にコミットしたコマンドバッファを待てばそれ以前の提出も終わっている。
			// 終了時と実行時のリソース破棄の両方で効く(設計書 §2.2)。
			@autoreleasepool
			{
				if ([objects_->lastCommandBuffer status] != MTLCommandBufferStatusNotEnqueued) {
					[objects_->lastCommandBuffer waitUntilCompleted];
				}
			}
		}


		void MetalGraphicsDeviceImpl::SetLastCommittedCommandBuffer(void* commandBuffer)
		{
			if (objects_ == nullptr) {
				return;
			}

			id<MTLCommandBuffer> next = static_cast<id<MTLCommandBuffer>>(commandBuffer);
			if (objects_->lastCommandBuffer == next) {
				return;
			}

			[next retain];
			[objects_->lastCommandBuffer release];
			objects_->lastCommandBuffer = next;
		}


		/**
		 * Metal オブジェクトへの口
		 */
		void* MetalGraphicsDeviceImpl::GetMTLDeviceHandle() const
		{
			return (objects_ != nullptr) ? objects_->device : nil;
		}


		void* MetalGraphicsDeviceImpl::GetMTLCommandQueueHandle() const
		{
			return (objects_ != nullptr) ? objects_->commandQueue : nil;
		}


		void* MetalGraphicsDeviceImpl::GetCAMetalLayerHandle() const
		{
			return (objects_ != nullptr) ? objects_->layer : nil;
		}


		/**
		 * RenderContext
		 */
		void MetalGraphicsDeviceImpl::SetupRenderContext(RenderContext& outContext)
		{
			auto impl = std::make_unique<MetalRenderContextImpl>(this);
			activeContext_ = impl.get();
			outContext.SetImpl(std::move(impl));
		}


		void MetalGraphicsDeviceImpl::SetupDefaultRenderState(RenderContext& /*context*/)
		{
			// Metal の既定ステートは MTLRenderPipelineState / MTLDepthStencilState 側で決まるため、
			// ここで設定するものは無い(Vulkan 版と同じ。設計書 §4)。
		}


		/**
		 * レンダーターゲット
		 */
		uint32_t MetalGraphicsDeviceImpl::GetMainRenderTargetCount() const
		{
			return MAIN_RT_COUNT;
		}


		IRenderTarget& MetalGraphicsDeviceImpl::GetMainRenderTarget(uint32_t index)
		{
			if (index >= MAIN_RT_COUNT) {
				index = 0;
			}
			return *mainRTs_[index];
		}


		IRenderTarget* MetalGraphicsDeviceImpl::GetRenderTarget(uint32_t index)
		{
			// index < MAIN_RT_COUNT がメイン、それ以降がオフスクリーン(Vulkan / D3D12 と同じ並び)。
			if (index < MAIN_RT_COUNT) {
				return mainRTs_[index].get();
			}

			const uint32_t offscreenIndex = index - MAIN_RT_COUNT;
			if (offscreenIndex < offscreenRTs_.size()) {
				return offscreenRTs_[offscreenIndex].get();
			}
			return nullptr;
		}


		uint32_t MetalGraphicsDeviceImpl::CreateOffscreenRenderTarget(uint32_t width, uint32_t height)
		{
			RenderTargetDesc desc;
			desc.width  = width;
			desc.height = height;
			return CreateOffscreenRenderTarget(desc);
		}


		uint32_t MetalGraphicsDeviceImpl::CreateOffscreenRenderTarget(const RenderTargetDesc& desc)
		{
			id<MTLDevice> device = ToMTLDevice(this);
			if (device == nil) {
				return INVALID_RT_INDEX;
			}

			MTLPixelFormat format = metal::ToMTLPixelFormat(desc.colorFormat);
			if (format == MTLPixelFormatInvalid) {
				format = MTLPixelFormatRGBA8Unorm;
			}

			auto renderTarget = std::make_unique<MetalRenderTarget>();
			if (!renderTarget->CreateOffscreen(device, desc.width, desc.height, format, desc.hasDepth)) {
				return INVALID_RT_INDEX;
			}

			offscreenRTs_.push_back(std::move(renderTarget));
			return MAIN_RT_COUNT + static_cast<uint32_t>(offscreenRTs_.size() - 1);
		}


		/**
		 * 提示
		 */
		void MetalGraphicsDeviceImpl::Present()
		{
			// TODO(P1): nextDrawable を取り、MTLRenderPassDescriptor でクリアしてから
			//           presentDrawable: / commit する(設計書 §2.2)。コミットしたコマンド
			//           バッファは SetLastCommittedCommandBuffer() へ記録すること。
		}


		void MetalGraphicsDeviceImpl::CopyToBackBuffer(IRenderTarget& /*src*/)
		{
			// TODO(P1): MTLBlitCommandEncoder の copyFromTexture:toTexture: で drawable へ写す。
			//           フォーマットが違う場合はフルスクリーン描画へフォールバックする(設計書 §7)。
		}


		/**
		 * リソースファクトリー
		 *
		 * いずれも nullptr を返さない。呼び出し元 (Resource / Rendering 層) が戻り値を
		 * 無条件に参照するため、生成に失敗しても空のオブジェクトを返して起動を通す。
		 */
		std::unique_ptr<IVertexBuffer> MetalGraphicsDeviceImpl::CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vertexBuffer = std::make_unique<MetalVertexBuffer>(ToMTLDevice(this), /*dynamic*/false);
			vertexBuffer->Create(vertexNum, stride, data);
			return vertexBuffer;
		}


		std::unique_ptr<IVertexBuffer> MetalGraphicsDeviceImpl::CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vertexBuffer = std::make_unique<MetalVertexBuffer>(ToMTLDevice(this), /*dynamic*/true);
			vertexBuffer->Create(vertexNum, stride, data);
			return vertexBuffer;
		}


		std::unique_ptr<IIndexBuffer> MetalGraphicsDeviceImpl::CreateIndexBuffer(uint32_t indexNum, const void* data)
		{
			auto indexBuffer = std::make_unique<MetalIndexBuffer>(ToMTLDevice(this), /*dynamic*/false);
			indexBuffer->Create(indexNum, data);
			return indexBuffer;
		}


		std::unique_ptr<IIndexBuffer> MetalGraphicsDeviceImpl::CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data)
		{
			// 動的 IB は 16bit / 32bit の両方が来るため、フォーマット指定版の Create を使う。
			auto indexBuffer = std::make_unique<MetalIndexBuffer>(ToMTLDevice(this), /*dynamic*/true);
			indexBuffer->Create(indexNum, format, data);
			return indexBuffer;
		}


		std::unique_ptr<IConstantBuffer> MetalGraphicsDeviceImpl::CreateConstantBuffer(const void* data, uint32_t size)
		{
			auto constantBuffer = std::make_unique<MetalConstantBuffer>(ToMTLDevice(this));
			constantBuffer->Create(data, size);
			return constantBuffer;
		}


		std::unique_ptr<IShader> MetalGraphicsDeviceImpl::CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type)
		{
			auto shader = std::make_unique<MetalShader>(ToMTLDevice(this));
			shader->Load(filePath, entryFunc, type);
			return shader;
		}


		std::unique_ptr<ISamplerState> MetalGraphicsDeviceImpl::CreateSamplerState(const SamplerDesc& desc)
		{
			auto sampler = std::make_unique<MetalSampler>(ToMTLDevice(this));
			sampler->Create(desc);
			return sampler;
		}


		std::unique_ptr<IShaderResourceView> MetalGraphicsDeviceImpl::CreateTexture2D(const Texture2DDesc& desc, const ImageData& data)
		{
			auto texture = std::make_unique<MetalTexture>(ToMTLDevice(this));
			texture->Create(desc, data);
			return texture;
		}


		std::unique_ptr<IDepthMap> MetalGraphicsDeviceImpl::CreateDepthMap(uint32_t width, uint32_t /*height*/)
		{
			// シャドウマップは正方形(resolution = width)。Vulkan / D3D12 版と同じ扱い。
			auto depthMap = std::make_unique<MetalDepthMap>();
			depthMap->Create(ToMTLDevice(this), width);
			return depthMap;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
