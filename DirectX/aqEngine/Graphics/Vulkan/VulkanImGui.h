#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"

struct ImDrawData;

namespace aq
{
	namespace graphics
	{
		// ── Vulkan 用 ImGui 接続層 (E1a) ──
		// 描画そのものは公式バックエンド imgui_impl_vulkan (v1.93.0-docking) が行う。
		// ここはエンジン固有の事情だけを引き受ける薄い層:
		//   - 初期化情報の組み立て (dynamic rendering・スワップチェーンのフォーマット/画像数)
		//   - スワップチェーンの作り直しへの追従 (フォーマット / 画像数が変わったら Shutdown → Init)
		//   - ImGui::Image に渡されたエンジンのハンドル (VulkanSRV*) をレイアウト遷移して
		//     VkDescriptorSet へ変換する (PrepareTextures)
		// 詳細は 設計書/エディタ全体設計.md 4.4。描画は device の CopyToBackBuffer 後に swapchain へ行う。
		namespace VulkanImGui
		{
			bool Init();
			void Shutdown();
			void NewFrame();

			/**
			 * 描画データのユーザーテクスチャを公式バックエンドが描ける形へ整える。
			 *
			 * dynamic rendering の scope 内ではレイアウト遷移を打てないので、
			 * 呼び出し側は vkCmdBeginRendering より前にこれを呼ぶ。
			 * 記録先は device の現在のコマンドバッファ (Render に渡すものと同じ)。
			 */
			void PrepareTextures(ImDrawData* drawData);

			/** 現在の swapchain (COLOR_ATTACHMENT) を対象に dynamic rendering 中の cmd へ記録する。 */
			void Render(VkCommandBuffer cmd, ImDrawData* drawData);
		}
	}
}
