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

			/**
			 * frames-in-flight を制御するセマフォ(初期値 FRAME_COUNT。設計書 §2.2)。
			 * BeginFrameIfNeeded で wait し、コマンドバッファの完了ハンドラで signal する。
			 */
			dispatch_semaphore_t frameSemaphore    = nullptr;

			/** このフレームの drawable とコマンドバッファ。どちらも autorelease なので retain して持つ */
			id<CAMetalDrawable>  currentDrawable      = nil;
			id<MTLCommandBuffer> currentCommandBuffer = nil;
		};


		namespace
		{
			/** 生成済みデバイス。リソースクラスからの静的アクセス用(Vulkan / D3D12 と同じ作法) */
			MetalGraphicsDeviceImpl* g_staticDevice = nullptr;

			/** CreateOffscreenRenderTarget の失敗値(IGraphicsDeviceImpl の契約) */
			static constexpr uint32_t INVALID_RT_INDEX = ~0u;

			/** CopyToBackBuffer の不一致ログ。毎フレーム出ると使い物にならないので 1 度だけ出す */
			bool g_copyMismatchLogged = false;

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
			, swapchainRT_()
			, activeContext_(nullptr)
			, frameCounter_(0)
			, frameOpen_(false)
			, frameAcquireFailed_(false)
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

			// compute の対応表明。
			//
			// TODO(P5): compute を実装したら true にする(Dispatch / CS* は P5 まで no-op)。
			//
			// Metal のハードウェアは当然 compute に対応しているが、**バックエンドの実装がまだ無い**。
			// ここで true を返すと Renderer が displayRT をポストプロセスチェーンの最終 RT
			// (Tonemap の compute 出力) にしてしまい、誰も書かない RT が CopyToBackBuffer で
			// 画面へ出る。false にすると Renderer はシーン RT を直接表示する経路に落ちるので、
			// P1〜P4 の「描いたものがそのまま見える」状態を保てる(Renderer::GetDisplayRTHandle)。
			// メイン RT を LDR の BGRA8Unorm で作っている現状とも整合する(設計書 §13-9)。
			aq::graphics::SetComputeSupported(false);

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

			// フレーム同期とスワップチェーンプロキシ(設計書 §2.2 / §7)。
			{
				objects_->frameSemaphore = dispatch_semaphore_create(metal::FRAME_COUNT);
				if (objects_->frameSemaphore == nullptr) {
					aq::StartupMark("  [metal] dispatch_semaphore_create failed");
					return false;
				}

				// 実体は毎フレーム nextDrawable のテクスチャを差し込む。ここでは器だけ用意する。
				swapchainRT_ = std::make_unique<MetalRenderTarget>();
				swapchainRT_->InitAsSwapchainProxy(width_, height_, metal::SWAPCHAIN_PIXEL_FORMAT);
				aq::StartupMarkf("  [metal] frame sync ok (frames in flight %u)", metal::FRAME_COUNT);
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

			// エンコーダは毎フレーム Present が閉じているので、ここでは触らない
			// (RenderContext の方が先に壊れている可能性があり、activeContext_ を触ると危ない)。

			// GPU が参照中かもしれないリソースを壊さないよう、先にアイドル化する(設計書 §2.2)。
			// waitUntilCompleted は完了ハンドラを呼び終えてから戻るので、
			// この後にセマフォを壊しても signal が空振りすることはない。
			WaitIdle();

			// 生成と逆順に解放する。
			offscreenRTs_.clear();
			for (auto& renderTarget : mainRTs_) {
				renderTarget.reset();
			}
			swapchainRT_.reset();
			activeContext_ = nullptr;

			@autoreleasepool
			{
				// コミットせずに残ったフレーム(drawable を取った直後に落ちた等)の後始末。
				if (frameOpen_)
				{
					[objects_->currentCommandBuffer release];
					[objects_->currentDrawable release];
					dispatch_semaphore_signal(objects_->frameSemaphore);
				}
				objects_->currentCommandBuffer = nil;
				objects_->currentDrawable      = nil;
				frameOpen_          = false;
				frameAcquireFailed_ = false;

				if (objects_->frameSemaphore != nullptr) {
					dispatch_release(objects_->frameSemaphore);
					objects_->frameSemaphore = nullptr;
				}

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


		void* MetalGraphicsDeviceImpl::GetCurrentCommandBufferHandle() const
		{
			return (objects_ != nullptr) ? objects_->currentCommandBuffer : nil;
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
		 * フレーム
		 */
		void MetalGraphicsDeviceImpl::BeginFrameIfNeeded()
		{
			// frameAcquireFailed_ は「このフレームは drawable が取れなかった」印。
			// CopyToBackBuffer と Present の両方から呼ばれるので、1 フレームに何度も
			// nextDrawable (nil 時は内部で待つ) を叩かないようここで止める。
			if (objects_ == nullptr || frameOpen_ || frameAcquireFailed_) {
				return;
			}

			// Initialize が途中で失敗した構成では何も始めない(セマフォへの wait で落ちるため)。
			if (objects_->frameSemaphore == nullptr || objects_->layer == nil || swapchainRT_ == nullptr) {
				return;
			}

			@autoreleasepool
			{
				// frames-in-flight 分だけ先行を許す。完了ハンドラが signal するまでここで待つ(設計書 §2.2)。
				dispatch_semaphore_wait(objects_->frameSemaphore, DISPATCH_TIME_FOREVER);

				// nextDrawable はウィンドウが隠れている等で **nil を返しうる**。
				// その場合はセマフォを戻してこのフレームを捨てる(落とさないこと)。
				id<CAMetalDrawable> drawable = [[objects_->layer nextDrawable] retain];
				if (drawable == nil) {
					dispatch_semaphore_signal(objects_->frameSemaphore);
					frameAcquireFailed_ = true;
					return;
				}

				id<MTLCommandBuffer> commandBuffer = [[objects_->commandQueue commandBuffer] retain];
				if (commandBuffer == nil) {
					[drawable release];
					dispatch_semaphore_signal(objects_->frameSemaphore);
					frameAcquireFailed_ = true;
					return;
				}

				objects_->currentDrawable      = drawable;
				objects_->currentCommandBuffer = commandBuffer;

				// drawable のテクスチャをスワップチェーンプロキシへ差し込む(設計書 §7)。
				swapchainRT_->SetDrawableTexture([drawable texture]);
				frameOpen_ = true;
			}
		}


		/**
		 * 提示
		 */
		void MetalGraphicsDeviceImpl::Present()
		{
			if (objects_ == nullptr) {
				return;
			}

			BeginFrameIfNeeded();
			if (!frameOpen_) {
				// drawable が取れなかったフレーム。次のフレームでは取り直す。
				frameAcquireFailed_ = false;
				return;
			}

			@autoreleasepool
			{
				// 予約されたままのクリアを畳み、開いているエンコーダを閉じてからコミットする。
				if (activeContext_ != nullptr) {
					activeContext_->FlushPendingClears();
					activeContext_->EndEncodingIfActive();
				}

				id<MTLCommandBuffer> commandBuffer = objects_->currentCommandBuffer;
				[commandBuffer presentDrawable:objects_->currentDrawable];

				// 完了ハンドラは **別スレッド** で呼ばれる。self を掴むと寿命が絡むので、
				// セマフォだけをローカルへ取り出してキャプチャし、中では signal しかしない。
				dispatch_semaphore_t frameSemaphore = objects_->frameSemaphore;
				[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> /*completed*/)
					{
						dispatch_semaphore_signal(frameSemaphore);
					}];
				[commandBuffer commit];

				// WaitIdle() の待機対象。記録し忘れると終了時と実行時リソース破棄で落ちる(設計書 §2.2)。
				SetLastCommittedCommandBuffer(commandBuffer);

				// フレーム状態のリセット。次の BeginFrameIfNeeded が効くようにする。
				swapchainRT_->SetDrawableTexture(nil);
				[objects_->currentDrawable release];
				objects_->currentDrawable = nil;
				[commandBuffer release];                 // BeginFrameIfNeeded の retain 分
				objects_->currentCommandBuffer = nil;

				frameOpen_          = false;
				frameAcquireFailed_ = false;
				++frameCounter_;
			}
		}


		uint32_t MetalGraphicsDeviceImpl::GetFrameIndex() const
		{
			return static_cast<uint32_t>(frameCounter_ % metal::FRAME_COUNT);
		}


		void MetalGraphicsDeviceImpl::CopyToBackBuffer(IRenderTarget& src)
		{
			if (objects_ == nullptr) {
				return;
			}

			BeginFrameIfNeeded();
			if (!frameOpen_) {
				return;
			}

			auto& renderTarget = static_cast<MetalRenderTarget&>(src);
			if (renderTarget.IsProxy()) {
				return;  // drawable 自身が渡された。写す必要が無い
			}

			// ブリットはエンコーダの外でしか積めない。保留クリアもここで畳んでおく
			// (P1 は描画が 1 本も無いので、実際に画面を塗るのはこの flush)。
			if (activeContext_ != nullptr) {
				activeContext_->FlushPendingClears();
				activeContext_->EndEncodingIfActive();
			}

			id<MTLTexture> srcTexture = renderTarget.GetTexture();
			id<MTLTexture> dstTexture = swapchainRT_->GetTexture();
			if (srcTexture == nil || dstTexture == nil) {
				return;
			}

			// TODO(P4): サイズ / フォーマットが違う場合はフルスクリーン描画へフォールバックする(設計書 §7)。
			//           ウィンドウのリサイズ(CAMetalLayer.drawableSize とメイン RT のずれ)もここに来る。
			//           P1 は落とさないことだけを担保し、1 度だけログを出してスキップする。
			if ([srcTexture pixelFormat] != [dstTexture pixelFormat] ||
			    [srcTexture width]       != [dstTexture width]       ||
			    [srcTexture height]      != [dstTexture height])
			{
				if (!g_copyMismatchLogged)
				{
					g_copyMismatchLogged = true;
					aq::StartupMarkf("  [metal] CopyToBackBuffer skipped: src %lux%lu fmt %lu / dst %lux%lu fmt %lu",
					                 static_cast<unsigned long>([srcTexture width]),
					                 static_cast<unsigned long>([srcTexture height]),
					                 static_cast<unsigned long>([srcTexture pixelFormat]),
					                 static_cast<unsigned long>([dstTexture width]),
					                 static_cast<unsigned long>([dstTexture height]),
					                 static_cast<unsigned long>([dstTexture pixelFormat]));
				}
				return;
			}

			@autoreleasepool
			{
				id<MTLBlitCommandEncoder> blit = [objects_->currentCommandBuffer blitCommandEncoder];
				[blit copyFromTexture:srcTexture
				          sourceSlice:0
				          sourceLevel:0
				         sourceOrigin:MTLOriginMake(0, 0, 0)
				           sourceSize:MTLSizeMake([srcTexture width], [srcTexture height], 1)
				            toTexture:dstTexture
				     destinationSlice:0
				     destinationLevel:0
				    destinationOrigin:MTLOriginMake(0, 0, 0)];
				[blit endEncoding];
			}
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
