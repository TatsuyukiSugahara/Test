#include "aq.h"
// 非 Metal 構成では本体をガードして空 TU にする(Vulkan バックエンドと同じ作法)。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalRenderTarget.h"

// 本 TU は手動参照カウント(MRR)前提。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalRenderTarget.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		MetalRenderTarget::MetalRenderTarget()
			: colorTexture_(nil)
			, drawableTexture_(nil)
			, colorFormat_(MTLPixelFormatInvalid)
			, depthTexture_(nil)
			, width_(0)
			, height_(0)
			, proxy_(false)
		{
			colorSRV_.owner = this;
			colorUAV_.owner = this;
		}


		MetalRenderTarget::~MetalRenderTarget()
		{
			Release();
		}


		bool MetalRenderTarget::CreateOffscreen(id<MTLDevice>  device,
		                                        const uint32_t width,
		                                        const uint32_t height,
		                                        MTLPixelFormat colorFormat,
		                                        const bool     hasDepth)
		{
			Release();
			if (device == nil || width == 0 || height == 0 || colorFormat == MTLPixelFormatInvalid) {
				return false;
			}

			proxy_       = false;
			width_       = width;
			height_      = height;
			colorFormat_ = colorFormat;

			@autoreleasepool
			{
				// カラー。RT 書き込み + 後続パスのサンプル + compute 書き込み(Bloom / Hi-Z)。
				// CPU からは触らないのでユニファイドメモリでも Private が最速。
				MTLTextureDescriptor* colorDesc =
					[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:colorFormat
					                                                   width:width
					                                                  height:height
					                                               mipmapped:NO];
				colorDesc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
				colorDesc.storageMode = MTLStorageModePrivate;

				colorTexture_ = [device newTextureWithDescriptor:colorDesc];  // MRR: +1
				if (colorTexture_ == nil) {
					Release();
					return false;
				}

				// 深度。Apple Silicon は D24S8 を持たないので Depth32Float 固定(設計書 §0.2)。
				if (hasDepth) {
					MTLTextureDescriptor* depthDesc =
						[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metal::DEPTH_PIXEL_FORMAT
						                                                   width:width
						                                                  height:height
						                                               mipmapped:NO];
					depthDesc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
					depthDesc.storageMode = MTLStorageModePrivate;

					depthTexture_ = [device newTextureWithDescriptor:depthDesc];  // MRR: +1
					if (depthTexture_ == nil) {
						Release();
						return false;
					}
				}
			}

			colorSRV_.owner = this;
			colorUAV_.owner = this;
			return true;
		}


		void MetalRenderTarget::InitAsSwapchainProxy(const uint32_t width, const uint32_t height, MTLPixelFormat colorFormat)
		{
			Release();

			proxy_       = true;
			width_       = width;
			height_      = height;
			colorFormat_ = colorFormat;

			colorSRV_.owner = this;
			colorUAV_.owner = this;
		}


		void MetalRenderTarget::SetDrawableTexture(id<MTLTexture> texture)
		{
			// drawable のテクスチャはフレーム毎に別物になる。エンコーダが握っている間に
			// 解放されないよう retain し、次の差し替えで前の分を release する。
			if (drawableTexture_ == texture) {
				return;
			}

			[texture retain];
			[drawableTexture_ release];
			drawableTexture_ = texture;
		}


		void MetalRenderTarget::Release()
		{
			// nil への release は no-op なので個別の判定は不要。
			[depthTexture_ release];
			depthTexture_ = nil;

			[colorTexture_ release];
			colorTexture_ = nil;

			[drawableTexture_ release];
			drawableTexture_ = nil;

			colorFormat_ = MTLPixelFormatInvalid;
			width_       = 0;
			height_      = 0;
			proxy_       = false;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
