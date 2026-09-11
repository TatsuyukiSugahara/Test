#include "aq.h"
// Metal のテクスチャ / サンプラ / UAV。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalResources.h"
#include <cstdio>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalResources.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** ブロック圧縮を含むフォーマットの寸法情報 */
			struct FormatInfo
			{
				uint32_t blockWidth;
				uint32_t blockHeight;
				uint32_t blockBytes;
			};


			/**
			 * PixelFormat のブロック寸法を返す
			 *
			 * BC は 4x4 ブロックで、BC1 / BC4 が 8 バイト、それ以外の BC が 16 バイト。
			 * 非圧縮は 1x1 ブロック(= 1 ピクセル)として扱う。
			 */
			FormatInfo GetFormatInfo(const PixelFormat format)
			{
				switch (format)
				{
				case PixelFormat::BC1_Unorm:
				case PixelFormat::BC1_Unorm_SRGB:
				case PixelFormat::BC4_Unorm:
					return { 4, 4, 8 };

				case PixelFormat::BC2_Unorm:
				case PixelFormat::BC2_Unorm_SRGB:
				case PixelFormat::BC3_Unorm:
				case PixelFormat::BC3_Unorm_SRGB:
				case PixelFormat::BC5_Unorm:
				case PixelFormat::BC6H_UFloat16:
				case PixelFormat::BC7_Unorm:
				case PixelFormat::BC7_Unorm_SRGB:
					return { 4, 4, 16 };

				case PixelFormat::R32G32B32A32_Float:  return { 1, 1, 16 };
				case PixelFormat::R16G16B16A16_Float:  return { 1, 1, 8 };
				case PixelFormat::R32_Float:           return { 1, 1, 4 };
				default:                               return { 1, 1, 4 };  // 8bit 4ch 系
				}
			}


			/**
			 * 1 サブリソースの密パッキング行バイト数と行数を返す
			 *
			 * BC はブロック行単位になるため、ピクセル行で計算すると壊れる(設計書 §6)。
			 */
			void TightLayout(const FormatInfo& info, const uint32_t mipWidth, const uint32_t mipHeight,
			                 uint32_t& outRowBytes, uint32_t& outRows)
			{
				const uint32_t blockCols = (mipWidth  + info.blockWidth  - 1) / info.blockWidth;
				const uint32_t blockRows = (mipHeight + info.blockHeight - 1) / info.blockHeight;
				outRowBytes = blockCols * info.blockBytes;
				outRows     = blockRows;
			}


			/** ミップ m の寸法(最小 1) */
			inline uint32_t MipExtent(const uint32_t base, const uint32_t mip)
			{
				const uint32_t v = base >> mip;
				return v ? v : 1u;
			}
		}


		/**
		 * テクスチャ 2D + SRV
		 */
		MetalTexture::MetalTexture(id<MTLDevice> device)
			: device_([device retain])
			, texture_(nil)
		{
		}


		MetalTexture::~MetalTexture()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalTexture::Create(const Texture2DDesc& desc, const ImageData& data)
		{
			@autoreleasepool
			{
				Release();
				if (device_ == nil || desc.width == 0 || desc.height == 0) {
					return false;
				}

				const MTLPixelFormat pixelFormat = metal::ToMTLPixelFormat(desc.format);
				if (pixelFormat == MTLPixelFormatInvalid) {
					char msg[128];
					std::snprintf(msg, sizeof(msg),
						"[MetalTexture] 未対応の PixelFormat(%u) でテクスチャを生成しようとしました",
						static_cast<uint32_t>(desc.format));
					aq::StartupLog(msg);
					return false;
				}

				const uint32_t mips   = desc.mipLevels ? desc.mipLevels : 1;
				const uint32_t layers = desc.arraySize ? desc.arraySize : 1;
				// キューブは 6 スライス固定で、arrayLength は 1(配列キューブは扱わない)。
				const uint32_t slices = desc.isCubemap ? 6u : layers;

				// 本体。ユニファイドメモリなので Shared に置いて CPU から直接書く。
				{
					MTLTextureDescriptor* textureDesc = [[MTLTextureDescriptor alloc] init];
					textureDesc.textureType      = desc.isCubemap ? MTLTextureTypeCube
					                             : ((layers > 1) ? MTLTextureType2DArray : MTLTextureType2D);
					textureDesc.pixelFormat      = pixelFormat;
					textureDesc.width            = desc.width;
					textureDesc.height           = desc.height;
					textureDesc.depth            = 1;
					textureDesc.mipmapLevelCount = mips;
					textureDesc.arrayLength      = desc.isCubemap ? 1 : layers;
					textureDesc.sampleCount      = 1;
					textureDesc.storageMode      = MTLStorageModeShared;
					textureDesc.usage            = MTLTextureUsageShaderRead;

					texture_ = [device_ newTextureWithDescriptor:textureDesc];
					[textureDesc release];
				}
				if (texture_ == nil) {
					aq::StartupLog("[MetalTexture] newTextureWithDescriptor に失敗しました");
					return false;
				}

				// サブリソースをそのまま流し込む(index = mip + slice * mips。Vulkan 版と同じ並び)。
				const FormatInfo info   = GetFormatInfo(desc.format);
				const bool       hasSub = (data.subresources && data.subresourceCount > 0);
				for (uint32_t s = 0; s < slices; ++s)
				{
					for (uint32_t m = 0; m < mips; ++m)
					{
						const uint32_t mipWidth  = MipExtent(desc.width,  m);
						const uint32_t mipHeight = MipExtent(desc.height, m);
						uint32_t rowBytes = 0;
						uint32_t rows     = 0;
						TightLayout(info, mipWidth, mipHeight, rowBytes, rows);

						const void* pixels      = nullptr;
						uint32_t    srcRowPitch = rowBytes;
						if (hasSub) {
							const uint32_t index = m + s * mips;
							if (index < data.subresourceCount) {
								pixels      = data.subresources[index].pixels;
								srcRowPitch = data.subresources[index].rowPitch ? data.subresources[index].rowPitch : rowBytes;
							}
						} else if (m == 0 && s == 0) {
							pixels      = data.pixels;
							srcRowPitch = data.rowPitch ? data.rowPitch : rowBytes;
						}
						if (!pixels) {
							continue;  // 初期データ無しのミップ / スライスは空のまま置く
						}

						// bytesPerRow は転送元の行ピッチ。BC はブロック行あたりのバイト数になる。
						[texture_ replaceRegion:MTLRegionMake2D(0, 0, mipWidth, mipHeight)
						            mipmapLevel:m
						                  slice:s
						              withBytes:pixels
						            bytesPerRow:srcRowPitch
						          bytesPerImage:0];
					}
				}
				return true;
			}
		}


		void MetalTexture::Release()
		{
			[texture_ release];
			texture_ = nil;
		}


		void* MetalTexture::GetNativeHandle() const
		{
			return static_cast<void*>(texture_);
		}


		/************************************/




		/**
		 * サンプラーステート
		 */
		MetalSampler::MetalSampler(id<MTLDevice> device)
			: device_([device retain])
			, sampler_(nil)
		{
		}


		MetalSampler::~MetalSampler()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalSampler::Create(const SamplerDesc& desc)
		{
			@autoreleasepool
			{
				Release();
				if (device_ == nil) {
					return false;
				}

				MTLSamplerMinMagFilter minMagFilter = MTLSamplerMinMagFilterLinear;
				MTLSamplerMipFilter    mipFilter    = MTLSamplerMipFilterLinear;
				bool                   anisotropic  = false;
				metal::ToMTLFilters(desc.filter, minMagFilter, mipFilter, anisotropic);

				MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
				samplerDesc.minFilter    = minMagFilter;
				samplerDesc.magFilter    = minMagFilter;
				samplerDesc.mipFilter    = mipFilter;
				samplerDesc.sAddressMode = metal::ToMTLAddressMode(desc.addressU);
				samplerDesc.tAddressMode = metal::ToMTLAddressMode(desc.addressV);
				samplerDesc.rAddressMode = metal::ToMTLAddressMode(desc.addressW);
				samplerDesc.borderColor  = MTLSamplerBorderColorOpaqueBlack;
				samplerDesc.lodMinClamp  = desc.minLOD;
				samplerDesc.lodMaxClamp  = desc.maxLOD;
				samplerDesc.maxAnisotropy = anisotropic ? ((desc.maxAniso > 1) ? desc.maxAniso : 1) : 1;
				// シャドウマップ用の比較サンプラ (SamplerComparisonState)。
				if (desc.isComparison) {
					samplerDesc.compareFunction = MTLCompareFunctionLessEqual;
				}
				// MTLSamplerDescriptor に LOD バイアスは無いため desc.mipLODBias は落ちる。
				// 実際に 0 以外を使い始めたらシェーダ側の SampleLevel で補うこと。

				sampler_ = [device_ newSamplerStateWithDescriptor:samplerDesc];
				[samplerDesc release];

				return sampler_ != nil;
			}
		}


		void MetalSampler::Release()
		{
			[sampler_ release];
			sampler_ = nil;
		}


		/************************************/




		/**
		 * アンオーダードアクセスビュー
		 */
		MetalUAV::MetalUAV()
			: texture_(nil)
			, buffer_(nil)
		{
		}


		MetalUAV::~MetalUAV()
		{
			Release();
		}


		void MetalUAV::Release()
		{
			// TODO(P5): compute を入れる段で、RT / 構造化バッファからの生成経路を足す。
			[texture_ release];
			texture_ = nil;
			[buffer_ release];
			buffer_  = nil;
		}


		void MetalUAV::SetTexture(id<MTLTexture> texture)
		{
			[texture retain];
			[texture_ release];
			texture_ = texture;
		}


		void MetalUAV::SetBuffer(id<MTLBuffer> buffer)
		{
			[buffer retain];
			[buffer_ release];
			buffer_ = buffer;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
