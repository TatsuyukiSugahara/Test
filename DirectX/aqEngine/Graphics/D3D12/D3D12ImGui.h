#pragma once

struct ImDrawData;
struct ID3D12GraphicsCommandList;


namespace aq
{
	namespace graphics
	{
		// DirectX12 用 ImGui バックエンドのグルー (Phase 4)。
		// imgui_impl_dx12 が要求するシェーダー可視 SRV ヒープとディスクリプタ割り当てを管理する。
		// ライフサイクル: Init() → 毎フレーム NewFrame() → 描画記録末尾で Render() → Shutdown()。
		namespace D3D12ImGui
		{
			bool Init();
			void Shutdown();
			void NewFrame();
			// 現在開いているコマンドリストへ ImGui の描画を記録する (RTV は呼び出し前にバインド済みのこと)。
			void Render(ImDrawData* drawData, ID3D12GraphicsCommandList* cmdList);
		}
	}
}
