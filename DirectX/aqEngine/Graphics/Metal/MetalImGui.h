#pragma once
// Metal 用 ImGui バックエンド(公式 imgui_impl_metal)への接続層(E1b-1。設計書「エディタ全体設計」4.5 節)。
//
// **本ヘッダは素の C++**。Core/AppHost.cpp(素の .cpp)が Init / Shutdown / NewFrame を
// 呼ぶため、Objective-C 型を表に出さない(設計書 MetalBackend設計 §10)。
// Metal の型を取る Render() だけは __OBJC__ ブロックへ隔離してあり、
// MetalGraphicsDeviceImpl.mm(Objective-C++)からのみ見える。
#if defined(ENGINE_GRAPHICS_METAL) && defined(AQ_IMGUI)
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

struct ImDrawData;
struct ImGuiContext;


namespace aq
{
	namespace graphics
	{
		// ── Metal 用 ImGui バックエンドの接続層 (E1b-1) ──
		// D3D12ImGui / VulkanImGui と同じ形。PSO・フォント等のテクスチャ・頂点/インデックスバッファは
		// 公式 imgui_impl_metal が持つ(動的フォントの RendererHasTextures にも対応)。
		// Metal の ImTextureID は id<MTLTexture> で、エンジンの GetNativeHandle() と同じなので変換は無い。
		// 描画は device の CopyToBackBuffer が drawable へ変換出力した**後**に重ねる。
		// スレッド: Init / Shutdown / NewFrame はメインスレッド、Render はレンダースレッド。
		// 公式のデータは Render(レンダースレッド)だけが触る(Init / Shutdown の間は描画していない)。
		// 公式のデータは current コンテキストの io.BackendRendererUserData にあるので、エディタ構成
		// (設計書「エディタ」E10 の 3.)はコンテキストごとに Init する。Render は current のコンテキストの
		// バックエンドで描くので、呼び出し側が描く方のコンテキストを current にしておく。
		namespace MetalImGui
		{
			bool Init();
			void Shutdown();
			void NewFrame();  // 何もしない(公式の NewFrame は Render の中で呼ぶ。理由は .mm)

			/**
			 * エディタ用コンテキストにも公式バックエンドを接続する(Init の後。メインスレッド)。
			 * エディタ用を current にして公式の Init を呼び、current は呼ぶ前のものへ戻す。
			 * @param gameCtx   ゲーム用コンテキスト(Init 済みであること)
			 * @param editorCtx エディタ用コンテキスト
			 * @return 接続できたら true
			 */
			bool InitEditor(ImGuiContext* gameCtx, ImGuiContext* editorCtx);

			/**
			 * InitEditor で接続したエディタ用コンテキストのバックエンドを畳む(Shutdown より前。メインスレッド)。
			 * current は呼ぶ前のものへ戻す。共有フォントアトラスのテクスチャの破棄は公式の参照数に任せる。
			 */
			void ShutdownEditor();

#ifdef __OBJC__
			/**
			 * 開いた(loadAction = Load / Clear の)レンダーパスへ、current のコンテキストの ImGui を記録する
			 *
			 * エンコーダの開閉と current の切り替えは呼び出し側(MetalGraphicsDeviceImpl の
			 * CopyToBackBuffer / ImGuiRender / RenderEditorOverlay)の担当。
			 * @param commandBuffer        記録先のコマンドバッファ(公式が完了ハンドラでバッファを回収する)
			 * @param encoder              記録先のレンダーコマンドエンコーダ
			 * @param renderPassDescriptor encoder を開いたレンダーパス記述子(PSO のフォーマットを決める)
			 * @param drawData             ImGui::Render() が作った描画データ
			 */
			void Render(id<MTLCommandBuffer>        commandBuffer,
			            id<MTLRenderCommandEncoder> encoder,
			            MTLRenderPassDescriptor*    renderPassDescriptor,
			            ImDrawData*                 drawData);
#endif
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL && AQ_IMGUI
