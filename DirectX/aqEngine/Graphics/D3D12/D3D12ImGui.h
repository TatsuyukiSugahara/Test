#pragma once

struct ImDrawData;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;


namespace aq
{
	namespace graphics
	{
		class D3D12SRV;

		// DirectX12 用 ImGui バックエンド (公式 imgui_impl_dx12) への接続層 (E1a)。
		// imgui_impl_dx12 が要求するシェーダー可視 SRV ヒープとディスクリプタ割り当てを管理し、
		// エンジンのテクスチャ (D3D12SRV*) を ImGui の ImTextureID へ変換する。
		// ライフサイクル: Init() → 毎フレーム NewFrame() → 描画記録末尾で Render() → Shutdown()。
		namespace D3D12ImGui
		{
			bool Init();
			void Shutdown();
			void NewFrame();
			// 現在開いているコマンドリストへ ImGui の描画を記録する (RTV は呼び出し前にバインド済みのこと)。
			void Render(ImDrawData* drawData, ID3D12GraphicsCommandList* cmdList);

			// エディタ用コンテキストへのバックエンド接続 (AQ_EDITOR 構成のみ使う)。
			// ヒープは Init() で作ったものを共有し、current なコンテキスト専用のバックエンド状態だけを作る。
			// 呼び出し元がエディタ用コンテキストを current にしてから呼ぶこと。Init() の後に呼ぶ。
			bool InitEditor();
			// エディタ用コンテキストのバックエンド状態を解放する (ヒープは解放しない)。
			// 呼び出し元がエディタ用コンテキストを current にしてから呼ぶこと。Shutdown() より前に呼ぶ。
			void ShutdownEditor();
			// エディタ用コンテキストの NewFrame。呼び出し元がエディタ用コンテキストを current にしてから呼ぶ。
			void NewFrameEditor();
			// 実フレームの先頭で 1 回だけ呼び、そのフレームのユーザーテクスチャ区画の書き込み位置を先頭へ戻す。
			// 1 フレームに Render() が複数回 (ゲーム用とエディタ用) 呼ばれても区画が重ならないようにするため。
			void ResetUserSlots(uint32_t frameIndex);

			// アルファを 1 に固定して表示するテクスチャ (ゲームビューの displayRT 用) を登録し、
			// ImGui::Image に渡せる ID を返す。同じリソースなら同じ ID を返すので毎フレーム呼んでよい。
			// format は SRV のフォーマット (DXGI_FORMAT)、srv は描画前の状態遷移に使う既存の SRV。
			void* RegisterOpaqueTexture(ID3D12Resource* resource, uint32_t format, D3D12SRV* srv);

			// 登録した表から resource のエントリを外す (RT の解放・作り直しの前に呼ぶ)。
			// 外した後の ID は無効になるが、ImGui のテクスチャ ID はパネルが毎フレーム
			// 取り直す契約 (設計書「エディタ全体設計」5.4 節) なので使い続ける者はいない。
			// GPU の完了待ちの後に呼ぶこと。
			void UnregisterOpaqueTexture(ID3D12Resource* resource);
		}
	}
}
