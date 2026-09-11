#pragma once
// Metal 用 ImGui 自前バックエンド(設計書/MetalBackend設計.md §12 P6)。
//
// **本ヘッダは素の C++**。Core/Application.cpp(素の .cpp)が Init / Shutdown / NewFrame を
// 呼ぶため、Objective-C 型を表に出さない(設計書 §10)。
// 唯一 id<MTLRenderCommandEncoder> を取る Render() だけは __OBJC__ ブロックへ隔離してあり、
// MetalGraphicsDeviceImpl.mm(Objective-C++)からのみ見える。
#if defined(ENGINE_GRAPHICS_METAL) && defined(AQ_IMGUI)
#include <cstdint>
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

struct ImDrawData;


namespace aq
{
	namespace graphics
	{
		// ── Metal 用 ImGui 自前バックエンド (P6) ──
		// VulkanImGui / D3D12ImGui 相当。imgui_impl_metal はコア(1.92 WIP 19193)と
		// 版数非互換のため使わず、クラシック API
		// (GetTexDataAsRGBA32 / ImDrawData::CmdLists / ImTextureID) で自前描画する。
		// 専用 PSO(頂点 col=RGBA8)+ フォントアトラス + 頂点/インデックスストリーミング。
		// 描画は device の CopyToBackBuffer が drawable へ変換出力した**後**に重ねる。
		namespace MetalImGui
		{
			bool Init();
			void Shutdown();
			void NewFrame();  // フォント等は Init 生成のため no-op

#ifdef __OBJC__
			/**
			 * drawable を対象に開いた(loadAction = Load の)レンダーパスへ ImGui を記録する
			 *
			 * エンコーダの開閉は呼び出し側(MetalGraphicsDeviceImpl::CopyToBackBuffer)の担当。
			 * ここではパイプライン / バッファ / シザーの設定と描画コマンドだけを積む。
			 * @param encoder      記録先のレンダーコマンドエンコーダ
			 * @param colorFormat  アタッチメントのピクセルフォーマット(PSO の生成キー)
			 * @param targetWidth  アタッチメントの幅 (px)。シザーのクランプに使う
			 * @param targetHeight アタッチメントの高さ (px)。同上
			 * @param drawData     ImGui::Render() が作った描画データ
			 */
			void Render(id<MTLRenderCommandEncoder> encoder,
			            MTLPixelFormat              colorFormat,
			            uint32_t                    targetWidth,
			            uint32_t                    targetHeight,
			            ImDrawData*                 drawData);
#endif
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL && AQ_IMGUI
