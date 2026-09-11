#include "aq.h"
// 非 Metal 構成では本体をガードして空 TU にする(Vulkan バックエンドと同じ作法)。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalDepthMap.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"

// 本 TU は手動参照カウント(MRR)前提。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalDepthMap.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** MTLSamplerDescriptor.maxAnisotropy の有効範囲 */
			static constexpr uint32_t MIN_ANISOTROPY = 1;
			static constexpr uint32_t MAX_ANISOTROPY = 16;

			/** 生成直後の深度。1.0 = 遠クリップ = 「何にも遮られていない」(D3D11 / Vulkan 版と同値) */
			static constexpr double INITIAL_CLEAR_DEPTH = 1.0;
		}


		/**
		 * シャドウ用の比較サンプラ
		 */
		MetalDepthMap::CompareSampler::CompareSampler()
			: device_(nil)
			, sampler_(nil)
		{
		}


		MetalDepthMap::CompareSampler::~CompareSampler()
		{
			Release();
		}


		bool MetalDepthMap::CompareSampler::Create(const SamplerDesc& desc)
		{
			Release();
			if (device_ == nil) {
				return false;
			}

			@autoreleasepool
			{
				MTLSamplerMinMagFilter minMagFilter = MTLSamplerMinMagFilterLinear;
				MTLSamplerMipFilter    mipFilter    = MTLSamplerMipFilterLinear;
				bool                   anisotropic  = false;
				metal::ToMTLFilters(desc.filter, minMagFilter, mipFilter, anisotropic);

				uint32_t maxAniso = anisotropic ? desc.maxAniso : MIN_ANISOTROPY;
				if (maxAniso < MIN_ANISOTROPY) { maxAniso = MIN_ANISOTROPY; }
				if (maxAniso > MAX_ANISOTROPY) { maxAniso = MAX_ANISOTROPY; }

				MTLSamplerDescriptor* samplerDesc = [[[MTLSamplerDescriptor alloc] init] autorelease];
				samplerDesc.minFilter     = minMagFilter;
				samplerDesc.magFilter     = minMagFilter;
				samplerDesc.mipFilter     = mipFilter;
				samplerDesc.sAddressMode  = metal::ToMTLAddressMode(desc.addressU);
				samplerDesc.tAddressMode  = metal::ToMTLAddressMode(desc.addressV);
				samplerDesc.rAddressMode  = metal::ToMTLAddressMode(desc.addressW);
				samplerDesc.maxAnisotropy = maxAniso;
				samplerDesc.lodMinClamp   = desc.minLOD;
				samplerDesc.lodMaxClamp   = desc.maxLOD;
				// シャドウマップの外側は「最遠 = 影なし」に落とすため白で埋める。
				samplerDesc.borderColor = MTLSamplerBorderColorOpaqueWhite;
				// MSL の sample_compare が要求する比較サンプラ。D3D11 / Vulkan 版と同じ LessEqual。
				samplerDesc.compareFunction = desc.isComparison ? MTLCompareFunctionLessEqual
				                                                : MTLCompareFunctionNever;

				sampler_ = [device_ newSamplerStateWithDescriptor:samplerDesc];  // MRR: +1
			}
			return sampler_ != nil;
		}


		void MetalDepthMap::CompareSampler::Release()
		{
			[sampler_ release];
			sampler_ = nil;
		}


		/************************************/




		/**
		 * 深度専用テクスチャ(4 スライスのカスケードシャドウ)
		 */
		MetalDepthMap::MetalDepthMap()
			: texture_(nil)
			, resolution_(0)
		{
			for (uint32_t slice = 0; slice < ARRAY_SIZE; ++slice) {
				sliceTextures_[slice] = nil;
			}
		}


		MetalDepthMap::~MetalDepthMap()
		{
			Release();
		}


		bool MetalDepthMap::Create(id<MTLDevice> device, const uint32_t resolution)
		{
			Release();
			if (device == nil || resolution == 0) {
				return false;
			}

			resolution_ = resolution;

			@autoreleasepool
			{
				// Depth32Float の 2D 配列(4 スライス)。シャドウパスで RT として書き、
				// メインパスで SRV として読むので usage は RenderTarget | ShaderRead。
				// 深度は CPU から触らないため storageMode は Private。
				MTLTextureDescriptor* desc = [[[MTLTextureDescriptor alloc] init] autorelease];
				desc.textureType      = MTLTextureType2DArray;
				desc.pixelFormat      = metal::DEPTH_PIXEL_FORMAT;
				desc.width            = resolution;
				desc.height           = resolution;
				desc.depth            = 1;
				desc.mipmapLevelCount = 1;
				desc.arrayLength      = ARRAY_SIZE;
				desc.sampleCount      = 1;
				desc.usage            = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
				desc.storageMode      = MTLStorageModePrivate;

				texture_ = [device newTextureWithDescriptor:desc];  // MRR: +1
				if (texture_ == nil) {
					Release();
					return false;
				}
				arraySRV_.texture = texture_;

				// スライス別の 2D ビュー。シャドウパスの depthAttachment とデバッグ表示に使う。
				for (uint32_t slice = 0; slice < ARRAY_SIZE; ++slice) {
					sliceTextures_[slice] =
						[texture_ newTextureViewWithPixelFormat:metal::DEPTH_PIXEL_FORMAT
						                            textureType:MTLTextureType2D
						                                 levels:NSMakeRange(0, 1)
						                                 slices:NSMakeRange(slice, 1)];  // MRR: +1
					if (sliceTextures_[slice] == nil) {
						Release();
						return false;
					}
					sliceSRVs_[slice].texture = sliceTextures_[slice];
				}
			}

			// 比較サンプラ。VulkanDepthMap と同じ設定(Border / Linear / LessEqual)。
			SamplerDesc samplerDesc;
			samplerDesc.filter       = FilterMode::MinMagMipLinear;
			samplerDesc.addressU     = AddressMode::Border;
			samplerDesc.addressV     = AddressMode::Border;
			samplerDesc.addressW     = AddressMode::Border;
			samplerDesc.isComparison = true;
			sampler_.SetDevice(device);
			if (!sampler_.Create(samplerDesc)) {
				Release();
				return false;
			}

			// **生成直後に全スライスを 1.0 で埋める**。詳細は ClearAllSlices() のコメント。
			ClearAllSlices();
			return true;
		}


		void MetalDepthMap::ClearAllSlices()
		{
			// **これが無いと未初期化の深度が「全面が影」と判定される**。
			// ライティングはこの深度マップを sample_compare して遮蔽を求めるため、中身がゴミだと
			// 「手前の草とキャラクターだけ真っ暗」という形で出る。1.0(遠クリップ)で埋めておけば、
			// シャドウパスが一度も描かれていないスライスも「影なし」として読まれる。
			// VulkanDepthMap が生成時に vkCmdClearDepthStencilImage で 1.0 を流し込んでいるのと同じ意図。
			//
			// Metal には「テクスチャを直接クリアする」コマンドが無いので、
			// **カラー 0 本 + depthAttachment(loadAction = Clear)だけの空のレンダーパス**を
			// スライスごとに開いて即閉じる。実際に塗るのは loadAction なので描画コマンドは要らない。
			MetalGraphicsDeviceImpl* device = MetalGraphicsDeviceImpl::GetInstance();
			if (device == nullptr || texture_ == nil) {
				return;
			}

			id<MTLCommandQueue> commandQueue = static_cast<id<MTLCommandQueue>>(device->GetMTLCommandQueueHandle());
			if (commandQueue == nil) {
				aq::StartupLog("[MetalDepthMap] コマンドキューが取れず初期クリアを行えませんでした(手前が暗くなります)");
				return;
			}

			@autoreleasepool
			{
				// フレーム用のコマンドバッファ(Present までコミットされない)とは別に 1 本立てる。
				// **深度マップの生成は起動時の 1 回だけ**なので、ここは同期で待ってしまってよい。
				id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];  // autorelease
				if (commandBuffer == nil) {
					aq::StartupLog("[MetalDepthMap] コマンドバッファを作れず初期クリアを行えませんでした");
					return;
				}

				for (uint32_t slice = 0; slice < ARRAY_SIZE; ++slice)
				{
					MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];  // autorelease

					// スライス別ビューではなく**配列テクスチャ + slice 指定**で差す。
					// MetalRenderContextImpl のシャドウパスと同じ組み方に揃えておく。
					MTLRenderPassDepthAttachmentDescriptor* attachment = descriptor.depthAttachment;
					attachment.texture     = texture_;
					attachment.level       = 0;
					attachment.slice       = slice;
					attachment.loadAction  = MTLLoadActionClear;
					attachment.clearDepth  = INITIAL_CLEAR_DEPTH;
					attachment.storeAction = MTLStoreActionStore;

					id<MTLRenderCommandEncoder> encoder =
						[commandBuffer renderCommandEncoderWithDescriptor:descriptor];  // autorelease
					[encoder endEncoding];
				}

				[commandBuffer commit];
				[commandBuffer waitUntilCompleted];
			}
		}


		void MetalDepthMap::Release()
		{
			sampler_.Release();

			// nil への release は no-op なので個別の判定は不要。
			for (uint32_t slice = 0; slice < ARRAY_SIZE; ++slice) {
				[sliceTextures_[slice] release];
				sliceTextures_[slice]     = nil;
				sliceSRVs_[slice].texture = nil;
			}

			[texture_ release];
			texture_          = nil;
			arraySRV_.texture = nil;
			resolution_       = 0;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
