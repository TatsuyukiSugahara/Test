#include "aq.h"
// 非 Metal 構成 / ImGui 無効構成では本体をガードして空 TU にする(VulkanImGui.cpp と同じ作法)。
#if defined(ENGINE_GRAPHICS_METAL) && defined(AQ_IMGUI)
#include "Graphics/Metal/MetalImGui.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_metal.h>

// 本 TU は手動参照カウント(MRR)前提。CMake は -fobjc-arc を渡していない。
// ARC が要るのは公式の imgui_impl_metal.mm だけで、CMake がそのファイルにだけ付けている。
// 本 TU は ObjC のオブジェクトを保持しない(受け取ったものを公式へ渡すだけ)。
#if __has_feature(objc_arc)
#error "MetalImGui.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif

// ─────────────────────────────────────────────────────────────────────────
// 公式バックエンド imgui_impl_metal への接続層 (E1b-1。設計書「エディタ全体設計」4.5 節)。
// PSO・フォント等のテクスチャ・頂点/インデックスバッファはバックエンドが持つため、
// ここが受け持つのは Init / Shutdown の中継と、Render での NewFrame + RenderDrawData の呼び出しだけ。
// ─────────────────────────────────────────────────────────────────────────

namespace aq
{
	namespace graphics
	{
		namespace MetalImGui
		{
			namespace
			{
				/** バックエンド初期化済みか (Init 失敗時に Shutdown / Render を呼ばないため) */
				bool g_initialized = false;
			}


			bool Init()
			{
				MetalGraphicsDeviceImpl* device = MetalGraphicsDeviceImpl::GetInstance();
				if (device == nullptr) return false;

				id<MTLDevice> mtlDevice = static_cast<id<MTLDevice>>(device->GetMTLDeviceHandle());
				if (mtlDevice == nil) return false;

				// 公式は device を strong プロパティで保持する(ARC 側で retain される)。
				if (!ImGui_ImplMetal_Init(mtlDevice)) {
					EngineAssertMsg(false, "ImGui Metal バックエンド初期化失敗");
					return false;
				}
				g_initialized = true;
				return true;
			}


			void Shutdown()
			{
				// フォント等のテクスチャを解放するので、呼び出し側が GPU の完了を待ってから呼ぶ
				// (AppHost::Finalize の WaitIdle。IGraphicsDeviceImpl::ImGuiShutdown の契約)。
				if (!g_initialized) return;
				ImGui_ImplMetal_Shutdown();
				g_initialized = false;
			}


			void NewFrame()
			{
				// 何もしない。公式の ImGui_ImplMetal_NewFrame は描画先のレンダーパス記述子
				// (カラーのフォーマット・サンプル数。PSO のキー)を要るが、メインスレッドの時点では
				// drawable が無く正しい記述子を渡せない。そのためレンダースレッドの Render() で、
				// 実際に使う記述子を渡して呼ぶ。これで公式のデータを触るのはレンダースレッドだけになる。
			}


			void Render(id<MTLCommandBuffer>        commandBuffer,
			            id<MTLRenderCommandEncoder> encoder,
			            MTLRenderPassDescriptor*    renderPassDescriptor,
			            ImDrawData*                 drawData)
			{
				if (!g_initialized || commandBuffer == nil || encoder == nil || renderPassDescriptor == nil || drawData == nullptr) {
					return;
				}

				// 頂点が 0 でもテクスチャ(動的フォント)の更新があり得るため、RenderDrawData は常に呼ぶ。
				// 一時オブジェクトの autorelease は呼び出し側(CopyToBackBuffer)の @autoreleasepool が受ける。
				ImGui_ImplMetal_NewFrame(renderPassDescriptor);
				ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, encoder);
			}
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL && AQ_IMGUI
