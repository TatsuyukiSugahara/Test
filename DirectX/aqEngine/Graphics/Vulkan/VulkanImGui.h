#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"

struct ImDrawData;

namespace aq
{
	namespace graphics
	{
		// ── Vulkan 用 ImGui 自前バックエンド (Phase 4) ──
		// D3D12ImGui 相当。imgui_impl_vulkan はコア(1.92 WIP 19193)と版数非互換のため使わず、
		// クラシック API (GetTexDataAsRGBA32 / ImDrawData::CmdLists / ImTextureID) で自前描画する。
		// 専用パイプライン (頂点 col=RGBA8) + フォントアトラス + 頂点/インデックスストリーミング。
		// 描画は device の CopyToBackBuffer 後に swapchain へ行う。
		namespace VulkanImGui
		{
			bool Init();
			void Shutdown();
			void NewFrame();  // フォント等は Init 生成のため no-op
			// 現在の swapchain (COLOR_ATTACHMENT) を対象に dynamic rendering 中の cmd へ記録する。
			void Render(VkCommandBuffer cmd, ImDrawData* drawData);
		}
	}
}
