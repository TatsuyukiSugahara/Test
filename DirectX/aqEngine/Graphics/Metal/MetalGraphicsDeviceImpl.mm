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
#ifdef AQ_IMGUI
#include "Graphics/Metal/MetalImGui.h"
#endif

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

			/**
			 * CopyToBackBuffer のフルスクリーン変換描画用(設計書 §7)。
			 * 起動時に 1 度だけ作り、宛先フォーマットが変わったときだけ作り直す。
			 */
			id<MTLLibrary>             blitLibrary   = nil;
			id<MTLRenderPipelineState> blitPipeline  = nil;
			id<MTLSamplerState>        blitSampler   = nil;
			MTLPixelFormat             blitDstFormat = MTLPixelFormatInvalid;
		};


		namespace
		{
			/** 生成済みデバイス。リソースクラスからの静的アクセス用(Vulkan / D3D12 と同じ作法) */
			MetalGraphicsDeviceImpl* g_staticDevice = nullptr;

			/** CreateOffscreenRenderTarget の失敗値(IGraphicsDeviceImpl の契約) */
			static constexpr uint32_t INVALID_RT_INDEX = ~0u;

			/** CopyToBackBuffer のフォールバックログ。毎フレーム出ると使い物にならないので 1 度だけ出す */
			bool g_copyFallbackLogged = false;

			/** CopyToBackBuffer の失敗ログ(PSO が作れなかった等)。同上 */
			bool g_copyFailureLogged = false;

			/**
			 * CopyToBackBuffer のフルスクリーン変換描画に使う MSL。
			 *
			 * **なぜ .fx ではなく .mm への埋め込みなのか**
			 * この描画は「バックバッファのフォーマットが表示 RT と違うときに変換して出す」という
			 * **Metal バックエンド内部の都合**で、エンジンのシェーダ資産ではない。.fx を増やすと
			 *  (a) 設計の「.fx は無改変で移植する」が崩れ、
			 *  (b) shader_entries.txt に載せる必要が出て Vulkan / D3D 側のビルドにも波及し、
			 *  (c) 起動時のシェーダコンパイル本数(§13-2 で問題視している)がさらに増える。
			 * ソース文字列を newLibraryWithSource: に通せば追加ファイルなしで完結するので、
			 * ここに直接置いている。コンパイルは起動時の 1 回だけ。
			 *
			 * **頂点バッファは使わない**。vertex_id からクリップ空間を覆う大三角形
			 * (-1,-1)-(3,-1)-(-1,3) を作る。全画面を 2 枚の三角形で覆うより
			 * 対角線上の重複シェーディングが無い分だけ速く、頂点記述子も要らない。
			 *
			 * **Y 反転について**(ここを間違えると上下逆さまになる)
			 *  - Metal のクリップ空間は D3D と同じく **y = +1 が画面の上端**
			 *    (OpenGL/Vulkan のような下端ではない)。
			 *  - Metal のテクスチャ座標は **v = 0 がテクスチャの先頭行(上端)**。
			 *  - よって「画面の上端 (y=+1)」に「テクスチャの上端 (v=0)」を貼るには
			 *    v = (1 - y) * 0.5 とする。u はそのまま u = (x + 1) * 0.5。
			 *  - src テクスチャはエンジンが同じ Metal のラスタライズ規約で描いたものなので、
			 *    blit 経路(生バイトコピー)と同じ向きで出る。**追加の反転は不要**。
			 */
			static const char* const FULLSCREEN_BLIT_MSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FullscreenVSOut
{
	float4 position [[position]];
	float2 uv;
};

vertex FullscreenVSOut aqFullscreenBlitVS(uint vertexId [[vertex_id]])
{
	// クリップ空間を覆う大三角形。頂点バッファは要らない。
	const float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };

	const float2 p = positions[vertexId];
	FullscreenVSOut out;
	out.position = float4(p, 0.0, 1.0);

	// クリップ空間は y = +1 が上端、テクスチャは v = 0 が上端。よって v は反転して取る。
	out.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
	return out;
}

fragment float4 aqFullscreenBlitPS(FullscreenVSOut in [[stage_in]],
                                   texture2d<float> src [[texture(0)]],
                                   sampler          samp [[sampler(0)]])
{
	// 変換はフォーマット間の読み替えだけ。トーンマップは既にポストプロセスが済ませている。
	return src.sample(samp, in.uv);
}
)MSL";


			/**
			 * フルスクリーン変換描画の PSO / ライブラリ / サンプラを用意する
			 *
			 * 宛先フォーマットが前回と同じなら何もしない。起動時に 1 度だけ通るのが正常系で、
			 * ここが毎フレーム走るようなら宛先フォーマットが揺れている。
			 * @param objects   デバイスオブジェクト群
			 * @param dstFormat 宛先(バックバッファ)のピクセルフォーマット
			 * @return 使える状態になったら true
			 */
			bool EnsureFullscreenBlitPipeline(MetalDeviceObjects* objects, MTLPixelFormat dstFormat)
			{
				if (objects == nullptr || objects->device == nil || dstFormat == MTLPixelFormatInvalid) {
					return false;
				}
				if (objects->blitPipeline != nil && objects->blitDstFormat == dstFormat) {
					return true;
				}

				@autoreleasepool
				{
					// ライブラリは宛先フォーマットに依存しないので 1 度だけ作る。
					if (objects->blitLibrary == nil)
					{
						NSError* error  = nil;
						NSString* source = [NSString stringWithUTF8String:FULLSCREEN_BLIT_MSL];
						// new... なので既に +1。追加の retain は要らない(付けると解放されない)。
						objects->blitLibrary = [objects->device newLibraryWithSource:source
						                                                     options:nil
						                                                       error:&error];  // MRR: +1
						if (objects->blitLibrary == nil)
						{
							aq::StartupMarkf("  [metal] fullscreen blit library failed: %s",
							                 (error != nil) ? [[error localizedDescription] UTF8String] : "unknown");
							return false;
						}
					}

					// サンプラも 1 度だけ。解像度が違う場合(リサイズ)に備えて線形補間 + クランプ。
					if (objects->blitSampler == nil)
					{
						MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
						samplerDesc.minFilter    = MTLSamplerMinMagFilterLinear;
						samplerDesc.magFilter    = MTLSamplerMinMagFilterLinear;
						samplerDesc.mipFilter    = MTLSamplerMipFilterNotMipmapped;
						samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
						samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;

						objects->blitSampler = [objects->device newSamplerStateWithDescriptor:samplerDesc];  // MRR: +1
						[samplerDesc release];
						if (objects->blitSampler == nil) {
							aq::StartupMark("  [metal] fullscreen blit sampler failed");
							return false;
						}
					}

					// PSO は colorAttachments[0].pixelFormat を持つので宛先フォーマットごとに要る。
					id<MTLFunction> vertexFunction   = [objects->blitLibrary newFunctionWithName:@"aqFullscreenBlitVS"];
					id<MTLFunction> fragmentFunction = [objects->blitLibrary newFunctionWithName:@"aqFullscreenBlitPS"];
					if (vertexFunction == nil || fragmentFunction == nil)
					{
						[vertexFunction release];
						[fragmentFunction release];
						aq::StartupMark("  [metal] fullscreen blit functions not found");
						return false;
					}

					MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
					pipelineDesc.vertexFunction                  = vertexFunction;
					pipelineDesc.fragmentFunction                = fragmentFunction;
					pipelineDesc.colorAttachments[0].pixelFormat = dstFormat;
					// 頂点記述子は付けない(頂点バッファを読まないため)。深度も使わない。

					NSError* error = nil;
					id<MTLRenderPipelineState> pipeline =
						[objects->device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];  // MRR: +1

					[pipelineDesc release];
					[vertexFunction release];
					[fragmentFunction release];

					if (pipeline == nil)
					{
						aq::StartupMarkf("  [metal] fullscreen blit pipeline failed: %s",
						                 (error != nil) ? [[error localizedDescription] UTF8String] : "unknown");
						return false;
					}

					[objects->blitPipeline release];
					objects->blitPipeline  = pipeline;
					objects->blitDstFormat = dstFormat;
				}
				return true;
			}

			/**
			 * 表示 RT を drawable へ写す本体(blit / フルスクリーン変換描画)
			 *
			 * CopyToBackBuffer から括り出してある。**ここでの早期 return が
			 * 呼び出し側の後続処理(ImGui の重ね描き)を飛ばさないようにする**のが目的で、
			 * 関数の中身は括り出す前と同じ。
			 * @param objects    デバイスオブジェクト群
			 * @param srcTexture 表示 RT のテクスチャ
			 * @param dstTexture drawable のテクスチャ
			 */
			void RecordCopyToDrawable(MetalDeviceObjects* objects,
			                          id<MTLTexture>      srcTexture,
			                          id<MTLTexture>      dstTexture)
			{
				// **blit は生バイトコピー**。フォーマットも寸法も完全一致するときだけ使える。
				//
				//  - RGBA8Unorm → BGRA8Unorm: copyFromTexture: は「通してしまう」が中身はバイト列の
				//    移送なので **R と B が入れ替わる**(実機で確認済み。赤が青になる)。
				//    compute 有効時の表示 RT はトーンマップ最終 RT(RGBA8Unorm)、drawable は
				//    BGRA8Unorm なので、**この組み合わせが常用**。つまり実際にはほぼ常に下の描画へ落ちる。
				//  - RGBA16Float → BGRA8Unorm: Validation がアサートで落とす。
				//  - 寸法違い(ウィンドウのリサイズ)は blit では縮尺できない。
				//
				// よって一致しないときはフルスクリーン描画で変換する(設計書 §7)。
				const bool canBlit = ([srcTexture pixelFormat] == [dstTexture pixelFormat]) &&
				                     ([srcTexture width]       == [dstTexture width])       &&
				                     ([srcTexture height]      == [dstTexture height]);

				@autoreleasepool
				{
					if (canBlit)
					{
						id<MTLBlitCommandEncoder> blit = [objects->currentCommandBuffer blitCommandEncoder];
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
						return;
					}

					// ここからフォールバックのフルスクリーン変換描画。
					if (!g_copyFallbackLogged)
					{
						g_copyFallbackLogged = true;
						aq::StartupMarkf("  [metal] CopyToBackBuffer uses fullscreen draw: src %lux%lu fmt %lu / dst %lux%lu fmt %lu",
						                 static_cast<unsigned long>([srcTexture width]),
						                 static_cast<unsigned long>([srcTexture height]),
						                 static_cast<unsigned long>([srcTexture pixelFormat]),
						                 static_cast<unsigned long>([dstTexture width]),
						                 static_cast<unsigned long>([dstTexture height]),
						                 static_cast<unsigned long>([dstTexture pixelFormat]));
					}

					if (!EnsureFullscreenBlitPipeline(objects, [dstTexture pixelFormat]))
					{
						if (!g_copyFailureLogged)
						{
							g_copyFailureLogged = true;
							aq::StartupMark("  [metal] CopyToBackBuffer skipped: fullscreen blit pipeline unavailable");
						}
						return;
					}

					// 全画素を三角形が覆うので、宛先の読み出しは要らない(DontCare で帯域を節約する)。
					MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
					passDesc.colorAttachments[0].texture     = dstTexture;
					passDesc.colorAttachments[0].loadAction  = MTLLoadActionDontCare;
					passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

					id<MTLRenderCommandEncoder> encoder =
						[objects->currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc];
					if (encoder == nil) {
						return;
					}

					// ビューポートはアタッチメント全面が既定。src と寸法が違う場合は
					// サンプラの線形補間が縮尺を吸収する。
					[encoder setRenderPipelineState:objects->blitPipeline];
					[encoder setFragmentTexture:srcTexture atIndex:0];
					[encoder setFragmentSamplerState:objects->blitSampler atIndex:0];
					[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
					[encoder endEncoding];
				}
			}

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
			, imguiDrawData_(nullptr)
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

			// compute の対応表明(P4)。
			//
			// P1〜P3 は false にしていた。true にすると Renderer::GetDisplayRTHandle() が
			// displayRT をポストプロセスチェーンの最終 RT(Tonemap の出力)に切り替えるため、
			// compute パスが動いていない段階では「誰も書いていない RT」が画面へ出てしまうからだった。
			// P4 でポストプロセス一式を通すので true へ戻す。これが無いとトーンマップが走らず、
			// 照明面が暗く・空が飽和したままになる(設計書 §12 の P4 / §13-9)。
			//
			// あわせてメイン RT も HDR(R16G16B16A16_Float)へ変えている(下の主 RT 生成を参照)。
			aq::graphics::SetComputeSupported(true);

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
			//
			// **HDR(R16G16B16A16_Float)+ 深度付き**。Vulkan 版(VK_FORMAT_R16G16B16A16_SFLOAT)/
			// D3D12 版と同じ構成で、LDR へ落とすのは最後の CopyToBackBuffer だけにする(設計書 §13-9)。
			// P0 では SWAPCHAIN_PIXEL_FORMAT(BGRA8Unorm)で作っていたが、それだと Bloom と
			// トーンマップの入力が 8bit に切り詰められ、Vulkan と明るさが合わない。
			{
				const MTLPixelFormat mainColorFormat = metal::ToMTLPixelFormat(PixelFormat::R16G16B16A16_Float);
				for (auto& renderTarget : mainRTs_)
				{
					renderTarget = std::make_unique<MetalRenderTarget>();
					if (!renderTarget->CreateOffscreen(objects_->device, width_, height_,
					                                   mainColorFormat, /*hasDepth*/true)) {
						aq::StartupMark("  [metal] main render target creation failed");
						return false;
					}
				}
				aq::StartupMarkf("  [metal] main render targets ok (x%u, %ux%u, HDR RGBA16Float)",
				                 MAIN_RT_COUNT, width_, height_);
			}

			// CopyToBackBuffer のフルスクリーン変換描画。メイン RT が HDR、トーンマップ最終 RT が
			// RGBA8Unorm、drawable が BGRA8Unorm と**どの経路でもフォーマットが一致しない**ので、
			// 起動時に作っておく(失敗しても起動は続ける。CopyToBackBuffer 側で 1 度だけログを出す)。
			{
				if (EnsureFullscreenBlitPipeline(objects_, metal::SWAPCHAIN_PIXEL_FORMAT)) {
					aq::StartupMark("  [metal] fullscreen blit pipeline ok");
				}
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

				// フルスクリーン変換描画のキャッシュ(生成と逆順)。
				[objects_->blitPipeline release];
				objects_->blitPipeline  = nil;
				objects_->blitDstFormat = MTLPixelFormatInvalid;
				[objects_->blitSampler release];
				objects_->blitSampler = nil;
				[objects_->blitLibrary release];
				objects_->blitLibrary = nil;

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

			// エンコーダは入れ子にできない。保留クリアを畳み、開いているエンコーダを閉じてから積む。
			if (activeContext_ != nullptr) {
				activeContext_->FlushPendingClears();
				activeContext_->EndEncodingIfActive();
			}

			id<MTLTexture> srcTexture = renderTarget.GetTexture();
			id<MTLTexture> dstTexture = swapchainRT_->GetTexture();
			if (srcTexture == nil || dstTexture == nil) {
				return;
			}

			RecordCopyToDrawable(objects_, srcTexture, dstTexture);

#ifdef AQ_IMGUI
			// imgui は変換出力が**終わった後**に drawable へ重ねて描く。
			// Vulkan 版(VulkanGraphicsDeviceImpl.cpp の CopyToBackBuffer 末尾)と同じ位置・同じ考え方で、
			// 既に出ている画を消さないよう **loadAction = Load** のレンダーパスを開く。
			if (imguiDrawData_ != nullptr)
			{
				@autoreleasepool
				{
					MTLRenderPassDescriptor* imguiPass = [MTLRenderPassDescriptor renderPassDescriptor];
					imguiPass.colorAttachments[0].texture     = dstTexture;
					imguiPass.colorAttachments[0].loadAction  = MTLLoadActionLoad;
					imguiPass.colorAttachments[0].storeAction = MTLStoreActionStore;

					id<MTLRenderCommandEncoder> imguiEncoder =
						[objects_->currentCommandBuffer renderCommandEncoderWithDescriptor:imguiPass];
					if (imguiEncoder != nil)
					{
						MetalImGui::Render(imguiEncoder,
						                   [dstTexture pixelFormat],
						                   static_cast<uint32_t>([dstTexture width]),
						                   static_cast<uint32_t>([dstTexture height]),
						                   imguiDrawData_);
						[imguiEncoder endEncoding];
					}
				}

				// 描画データはこのフレーム限り。次のフレームで積み直されるまで持ち越さない。
				imguiDrawData_ = nullptr;
			}
#endif
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


		/**
		 * GPU 駆動用バッファ
		 *
		 * 上の各ファクトリーと違い、**失敗時は nullptr を返す**。呼び出し元の
		 * GpuClusterBuffers::Create() が 4 本すべて非 null であることを条件に
		 * clusterCount を立てるので、空オブジェクトを返すとカリングが壊れた状態で走る。
		 */
		std::unique_ptr<IGpuBuffer> MetalGraphicsDeviceImpl::CreateStructuredBuffer(uint32_t stride, uint32_t count, const void* data)
		{
			if (stride == 0 || count == 0) {
				return nullptr;
			}

			const uint32_t byteSize = stride * count;

			auto gpuBuffer = std::make_unique<MetalGpuBuffer>();
			if (!gpuBuffer->Create(ToMTLDevice(this), byteSize, stride,
			                       /*srv*/true, /*uav*/false, data, byteSize)) {
				return nullptr;
			}
			return gpuBuffer;
		}


		std::unique_ptr<IGpuBuffer> MetalGraphicsDeviceImpl::CreateRawBuffer(uint32_t byteSize, bool srv, bool uav, const void* initData)
		{
			if (byteSize == 0) {
				return nullptr;
			}

			// RAW ビューは 4 バイト要素。間接引数(20 バイト)のような半端な大きさが来るので、
			// D3D12 版と同じく 16 バイト境界へ切り上げて安全側に倒す。
			const uint32_t alignedSize = (byteSize + 15u) & ~15u;

			auto gpuBuffer = std::make_unique<MetalGpuBuffer>();
			if (!gpuBuffer->Create(ToMTLDevice(this), alignedSize, /*stride*/0, srv, uav, initData, byteSize)) {
				return nullptr;
			}
			return gpuBuffer;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
