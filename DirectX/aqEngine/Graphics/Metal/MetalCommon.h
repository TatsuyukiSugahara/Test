#pragma once
// Metal バックエンドの共通定義。
//
// **このヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
// エンジン側から見える公開ヘッダ(MetalGraphicsDeviceImpl.h 等)は素の C++ のままにし、
// Objective-C 型は各クラスの不透明構造体へ隠す(設計書/MetalBackend設計.md §10)。
#if defined(ENGINE_GRAPHICS_METAL)
#ifndef __OBJC__
#error "MetalCommon.h は Objective-C++ (.mm) からのみ include できます"
#endif

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>   // TARGET_OS_SIMULATOR (METALLIB_SDK_DIR_NAME の分岐)

#include "Platform/Common/PlatformDefs.h"   // AQ_PLATFORM_IOS (MSL_DIR_NAME の分岐)
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IRenderContextImpl.h"   // DepthMode / BlendMode


namespace aq
{
	namespace graphics
	{
		namespace metal
		{
			// ----------------------------------------------------------------
			//  フレーム
			// ----------------------------------------------------------------

			/** frames-in-flight 数。Vulkan バックエンドと揃える(設計書 §2.1) */
			static constexpr uint32_t FRAME_COUNT = 2;


			// ----------------------------------------------------------------
			//  シェーダの配置 (設計書/iOS移植設計.md §4.2)
			// ----------------------------------------------------------------

			/**
			 * ビルド時に生成した MSL(.metal)と、その中間生成物 .spv を置くディレクトリ名。
			 *
			 * **macOS 版 MSL と iOS 版 MSL は別物**。spirv-cross は既定で macOS 版を吐き、
			 * iOS 版は --msl-ios を付けて別に生成する。テクスチャ/サンプラの扱いなどが
			 * 異なるため、macOS 版をそのまま iOS へ持っていっても
			 * newLibraryWithSource: が通らない。同名で中身が別物のファイルが混ざると
			 * 壊れるので、Vulkan の spv/ と Metal の msl/ を分けているのと同じ理由で
			 * ディレクトリごと分ける。
			 *
			 * Tools/ShaderCompile/compile_msl.cmake の出力先(AQ_MSL_OUT_DIR)と
			 * **一対一で対応**させること。片方だけ変えないこと。
			 *
			 * 読み出し側は MetalShader.mm の BuildMslPath() / BuildSpirvPath() と
			 * MetalRenderContextImpl.mm の BuildComputeSpirvPath() の 3 箇所。
			 * その 3 箇所がずれないよう、名前はここ 1 箇所だけで持つ。
			 */
#if defined(AQ_PLATFORM_IOS)
			static constexpr char MSL_DIR_NAME[] = "msl-ios";


			/**
			 * ビルド時に焼いた .metallib を置くサブディレクトリ名(iOS 専用)。
			 *
			 * **iOS 実機では newLibraryWithSource: が使えない。**呼んだ瞬間に
			 * SIGBUS(signal 10)でアプリが即死する(ソースの内容とは無関係。
			 * 3 行の最小シェーダでも落ちる)。シミュレータでは同じコードが動くため
			 * P1〜P5a では踏めなかった。したがって iOS は .metallib を事前ビルドし、
			 * newLibraryWithURL: で読む(設計書/iOS移植設計.md §4.6)。
			 *
			 * **.metallib は SDK ごとに別物**(実機とシミュレータでは GPU も ABI も違う)
			 * なので、msl-ios/ の下をさらに SDK 名で分ける。**どちらを読むかは
			 * ビルド時ではなく実行中のバイナリで決まる**ので、TARGET_OS_SIMULATOR
			 * (シミュレータ向けにコンパイルされたときだけ 1)で判定する。
			 *
			 * 生成側は Tools/ShaderCompile/compile_msl.cmake の AQ_MSL_METALLIB_DIR
			 * (SDK は AQ_MSL_IOS_SDK。ルート CMakeLists.txt が CMAKE_OSX_SYSROOT から決める)。
			 * **一対一で対応している。片方だけ変えないこと。**
			 *
			 * 読み出し側は MetalShader.mm の BuildMslPath() と
			 * MetalGraphicsDeviceImpl.mm の EnsureFullscreenBlitPipeline() の 2 箇所。
			 */
#if TARGET_OS_SIMULATOR
			static constexpr char METALLIB_SDK_DIR_NAME[] = "iphonesimulator";
#else
			static constexpr char METALLIB_SDK_DIR_NAME[] = "iphoneos";
#endif
#else
			static constexpr char MSL_DIR_NAME[] = "msl";
#endif


			// ----------------------------------------------------------------
			//  バインディング規約 (設計書 §5)
			// ----------------------------------------------------------------
			//
			//  dxc のシフトを b:0 / t:8 / s:0 / u:24 にし、spirv-cross へ
			//  --msl-decoration-binding を渡すことで、SPIR-V の binding が
			//  そのまま Metal の index になる。
			//
			//    b0..bN (cbuffer)  -> [[buffer(0+N)]]
			//    t0..tN (SRV)      -> [[texture(8+N)]]  または [[buffer(8+N)]]
			//    s0..sN (Sampler)  -> [[sampler(0+N)]]
			//    u0..uN (UAV)      -> [[texture(24+N)]] または [[buffer(24+N)]]
			//
			//  **なぜ全部 0 始まりにしないのか**(P0.5 で踏んだ):
			//  Metal のバッファ / テクスチャ / サンプラは独立した番号空間なので、
			//  一見すると全部 0 から始めてよさそうに見える。しかし**中間生成物の
			//  SPIR-V は Vulkan の統一 binding 名前空間**で、そこでバッファ同士が
			//  衝突すると spirv-cross が
			//      device void* spvBufferAliasSet0Binding0 [[buffer(0)]]
			//  のような別名を作り、アドレス空間をまたぐ不正なキャストを吐く。
			//  ClusterCull.fx が StructuredBuffer を t0 / cbuffer を b0 に置いており、
			//  b と t を両方 0 にするとこれで壊れた。
			//  **b / t / u は SPIR-V 上で重ならないように配ること。**
			//  s だけは重なってよい(サンプラはバッファと別名化しない)。
			//
			//  値は Tools/ShaderCompile/dxc_args_metal.txt と**必ず一致させること**。

			/** t レジスタのシフト量。dxc の -fvk-t-shift と同じ値でなければならない */
			static constexpr uint32_t SRV_INDEX_SHIFT = 8;

			/** u レジスタのシフト量。dxc の -fvk-u-shift と同じ値でなければならない */
			static constexpr uint32_t UAV_INDEX_SHIFT = 24;

			/**
			 * 頂点バッファの Metal buffer index。
			 *
			 * Metal では頂点バッファも buffer 空間を共有するため、cbuffer(b0..)と
			 * ぶつからないよう上端から取る(設計書 §5.2)。
			 */
			static constexpr uint32_t VERTEX_BUFFER_INDEX   = 30;  // per-vertex   ストリーム
			static constexpr uint32_t INSTANCE_BUFFER_INDEX = 29;  // per-instance ストリーム

			/** 定数バッファの整列。Metal の constant アドレス空間の要求に合わせる */
			static constexpr uint32_t CONSTANT_BUFFER_ALIGNMENT = 256;


			// ----------------------------------------------------------------
			//  列挙の写像
			// ----------------------------------------------------------------

			/**
			 * PixelFormat -> MTLPixelFormat。
			 *
			 * D24_Unorm_S8_Uint は **Apple Silicon が非対応**なので Depth32Float へ読み替える
			 * (実機確認済み。設計書 §0.2 / §13-6)。
			 * BC1〜BC7 はそのまま使える(再エンコード不要)。
			 */
			inline MTLPixelFormat ToMTLPixelFormat(const PixelFormat format)
			{
				switch (format)
				{
				case PixelFormat::R8G8B8A8_Unorm:      return MTLPixelFormatRGBA8Unorm;
				case PixelFormat::R8G8B8A8_Unorm_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
				case PixelFormat::B8G8R8A8_Unorm:      return MTLPixelFormatBGRA8Unorm;
				case PixelFormat::B8G8R8A8_Unorm_SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;

				// Apple Silicon は D24S8 を持たない。深度は一律 32bit float。
				case PixelFormat::D24_Unorm_S8_Uint:   return MTLPixelFormatDepth32Float;

				case PixelFormat::R16G16B16A16_Float:  return MTLPixelFormatRGBA16Float;
				case PixelFormat::R32_Float:           return MTLPixelFormatR32Float;
				case PixelFormat::R32G32B32A32_Float:  return MTLPixelFormatRGBA32Float;

				case PixelFormat::BC1_Unorm:           return MTLPixelFormatBC1_RGBA;
				case PixelFormat::BC1_Unorm_SRGB:      return MTLPixelFormatBC1_RGBA_sRGB;
				case PixelFormat::BC2_Unorm:           return MTLPixelFormatBC2_RGBA;
				case PixelFormat::BC2_Unorm_SRGB:      return MTLPixelFormatBC2_RGBA_sRGB;
				case PixelFormat::BC3_Unorm:           return MTLPixelFormatBC3_RGBA;
				case PixelFormat::BC3_Unorm_SRGB:      return MTLPixelFormatBC3_RGBA_sRGB;
				case PixelFormat::BC4_Unorm:           return MTLPixelFormatBC4_RUnorm;
				case PixelFormat::BC5_Unorm:           return MTLPixelFormatBC5_RGUnorm;
				case PixelFormat::BC6H_UFloat16:       return MTLPixelFormatBC6H_RGBUfloat;
				case PixelFormat::BC7_Unorm:           return MTLPixelFormatBC7_RGBAUnorm;
				case PixelFormat::BC7_Unorm_SRGB:      return MTLPixelFormatBC7_RGBAUnorm_sRGB;

				case PixelFormat::Unknown:
				default:                               return MTLPixelFormatInvalid;
				}
			}


			/** スワップチェーン(CAMetalLayer)のフォーマット。Windows の既定と揃える */
			static constexpr MTLPixelFormat SWAPCHAIN_PIXEL_FORMAT = MTLPixelFormatBGRA8Unorm;

			/** 深度のフォーマット。Apple Silicon の都合で 32bit float 固定(上記参照) */
			static constexpr MTLPixelFormat DEPTH_PIXEL_FORMAT = MTLPixelFormatDepth32Float;


			/** PrimitiveTopology -> MTLPrimitiveType (描画コマンドに渡す) */
			inline MTLPrimitiveType ToMTLPrimitiveType(const PrimitiveTopology topology)
			{
				switch (topology)
				{
				case PrimitiveTopology::TriangleList:  return MTLPrimitiveTypeTriangle;
				case PrimitiveTopology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
				case PrimitiveTopology::LineList:      return MTLPrimitiveTypeLine;
				case PrimitiveTopology::LineStrip:     return MTLPrimitiveTypeLineStrip;
				case PrimitiveTopology::PointList:     return MTLPrimitiveTypePoint;
				default:                               return MTLPrimitiveTypeTriangle;
				}
			}


			/**
			 * PrimitiveTopology -> MTLPrimitiveTopologyClass (PSO に渡す)。
			 *
			 * PSO 側は point / line / triangle の 3 種別しか持たないため、
			 * List と Strip は同じ値に潰れる。PSO キーにはこちらを入れる(設計書 §4.1)。
			 */
			inline MTLPrimitiveTopologyClass ToMTLTopologyClass(const PrimitiveTopology topology)
			{
				switch (topology)
				{
				case PrimitiveTopology::PointList:     return MTLPrimitiveTopologyClassPoint;
				case PrimitiveTopology::LineList:
				case PrimitiveTopology::LineStrip:     return MTLPrimitiveTopologyClassLine;
				case PrimitiveTopology::TriangleList:
				case PrimitiveTopology::TriangleStrip:
				default:                               return MTLPrimitiveTopologyClassTriangle;
				}
			}


			/** IndexFormat -> MTLIndexType */
			inline MTLIndexType ToMTLIndexType(const IndexFormat format)
			{
				return (format == IndexFormat::UInt16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
			}


			/** FilterMode -> min/mag/mip の 3 つ + 異方性の有無 */
			inline void ToMTLFilters(const FilterMode              mode,
			                         MTLSamplerMinMagFilter&       outMinMag,
			                         MTLSamplerMipFilter&          outMip,
			                         bool&                         outAnisotropic)
			{
				outAnisotropic = false;
				switch (mode)
				{
				case FilterMode::MinMagMipPoint:
					outMinMag = MTLSamplerMinMagFilterNearest;
					outMip    = MTLSamplerMipFilterNearest;
					break;
				case FilterMode::MinMagLinearMipPoint:
					outMinMag = MTLSamplerMinMagFilterLinear;
					outMip    = MTLSamplerMipFilterNearest;
					break;
				case FilterMode::Anisotropic:
					outMinMag      = MTLSamplerMinMagFilterLinear;
					outMip         = MTLSamplerMipFilterLinear;
					outAnisotropic = true;
					break;
				case FilterMode::MinMagMipLinear:
				default:
					outMinMag = MTLSamplerMinMagFilterLinear;
					outMip    = MTLSamplerMipFilterLinear;
					break;
				}
			}


			/**
			 * サンプラのボーダーカラーが使えるデバイスか。
			 *
			 * ボーダーカラー(と ClampToBorderColor)は **GPU family Apple7 以上 / Mac2** が要る。
			 * それ未満で `borderColor` を設定するか ClampToBorderColor を渡すと、
			 * Metal の **Validation がアサートで即死**する:
			 *   `MTLSamplerBorderColorOpaqueWhite is not supported on this device`
			 * (iOS シミュレータは family Apple2 なので実際に踏んだ。設計書/iOS移植設計.md §4.7)
			 *
			 * 値は MetalGraphicsDeviceImpl::Initialize が実測して設定する。
			 * 呼べるのはデバイス初期化後だけ(それ以前は保守的に false)。
			 */
			bool IsSamplerBorderColorSupported();
			void SetSamplerBorderColorSupported(bool supported);


			/**
			 * AddressMode -> MTLSamplerAddressMode
			 *
			 * Border は非対応デバイスでは ClampToEdge へ落とす(上記参照)。
			 * 端の 1 テクセルが引き伸ばされるだけで、描画が止まるよりはるかにましという判断。
			 */
			inline MTLSamplerAddressMode ToMTLAddressMode(const AddressMode mode)
			{
				switch (mode)
				{
				case AddressMode::Wrap:   return MTLSamplerAddressModeRepeat;
				case AddressMode::Mirror: return MTLSamplerAddressModeMirrorRepeat;
				case AddressMode::Border: return IsSamplerBorderColorSupported()
				                               ? MTLSamplerAddressModeClampToBorderColor
				                               : MTLSamplerAddressModeClampToEdge;
				case AddressMode::Clamp:
				default:                  return MTLSamplerAddressModeClampToEdge;
				}
			}


			/**
			 * DepthMode -> MTLDepthStencilDescriptor の中身。
			 *
			 * Metal では深度は PSO ではなく MTLDepthStencilState という別オブジェクトで、
			 * エンコーダへ独立に設定する。そのため PSO キーには入れない(設計書 §4.1 / §4.3)。
			 */
			inline void ToMTLDepthState(const DepthMode      mode,
			                            MTLCompareFunction&  outCompare,
			                            bool&                outWriteEnabled)
			{
				switch (mode)
				{
				case DepthMode::ReadOnly:
					outCompare      = MTLCompareFunctionLess;
					outWriteEnabled = false;
					break;
				case DepthMode::Disabled:
					outCompare      = MTLCompareFunctionAlways;
					outWriteEnabled = false;
					break;
				case DepthMode::ReadWrite:
				default:
					outCompare      = MTLCompareFunctionLess;
					outWriteEnabled = true;
					break;
				}
			}


			/**
			 * BlendMode -> MTLRenderPipelineColorAttachmentDescriptor の設定。
			 *
			 * DecalColor は RGB のみ合成し、アルファチャンネルの書き込みを止める
			 * (GBuffer0.a = metallic を守るため。IRenderContextImpl.h のコメント参照)。
			 */
			inline void ApplyBlendMode(MTLRenderPipelineColorAttachmentDescriptor* attachment,
			                           const BlendMode                             mode)
			{
				attachment.writeMask = MTLColorWriteMaskAll;

				if (mode == BlendMode::Opaque)
				{
					attachment.blendingEnabled = NO;
					return;
				}

				attachment.blendingEnabled             = YES;
				attachment.rgbBlendOperation           = MTLBlendOperationAdd;
				attachment.alphaBlendOperation         = MTLBlendOperationAdd;
				attachment.sourceAlphaBlendFactor      = MTLBlendFactorOne;
				attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

				switch (mode)
				{
				case BlendMode::AlphaBlend:
					attachment.sourceRGBBlendFactor      = MTLBlendFactorSourceAlpha;
					attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
					break;

				case BlendMode::Additive:
					attachment.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
					attachment.destinationRGBBlendFactor   = MTLBlendFactorOne;
					attachment.destinationAlphaBlendFactor = MTLBlendFactorOne;
					break;

				case BlendMode::Premultiplied:
					attachment.sourceRGBBlendFactor      = MTLBlendFactorOne;
					attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
					break;

				case BlendMode::DecalColor:
					attachment.sourceRGBBlendFactor      = MTLBlendFactorSourceAlpha;
					attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
					// アルファは触らせない(= metallic を保護)。
					attachment.writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
					break;

				default:
					attachment.blendingEnabled = NO;
					break;
				}
			}
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
