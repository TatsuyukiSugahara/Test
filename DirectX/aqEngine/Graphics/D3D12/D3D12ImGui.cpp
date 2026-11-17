#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
// ImGui 専用バックエンド。AQ_IMGUI 未定義(Xbox 構成など)では空 TU にする。
#ifdef AQ_IMGUI
#include "D3D12Common.h"
#include "D3D12ImGui.h"
#include "D3D12GraphicsDeviceImpl.h"
#include "D3D12Resources.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx12.h>

// ─────────────────────────────────────────────────────────────────────────
// 公式バックエンド imgui_impl_dx12 への接続層 (E1a。設計書「エディタ全体設計」4.4 節)。
// PSO・VB/IB・フォントテクスチャはバックエンドが持つため、ここが受け持つのは
//  - バックエンドへ渡す shader-visible SRV ヒープの確保と区画の割り当て
//  - エンジンのテクスチャ (D3D12SRV*) を ImGui 用ヒープへ写す ImTextureID の変換
// だけになる。
//
// ヒープの区画 (全 HEAP_SLOT_COUNT 個):
//   [0, BACKEND_SLOT_COUNT)               バックエンドが Alloc/Free で使う領域 (フォントアトラス等)
//   [BACKEND_SLOT_COUNT, HEAP_SLOT_COUNT) ユーザーテクスチャ用。D3D12_FRAME_COUNT 個の
//                                         フレーム別区画 (各 USER_SLOT_COUNT 個) に分ける。
// ─────────────────────────────────────────────────────────────────────────

namespace aq
{
	namespace graphics
	{
		namespace D3D12ImGui
		{
			namespace
			{
				/** バックエンド (フォントアトラス等) が確保する領域のスロット数 */
				static constexpr uint32_t BACKEND_SLOT_COUNT = 128;
				/** ユーザーテクスチャ区画のスロット数 (1 フレームあたり) */
				static constexpr uint32_t USER_SLOT_COUNT    = 64;
				/** ヒープ全体のスロット数 */
				static constexpr uint32_t HEAP_SLOT_COUNT    = BACKEND_SLOT_COUNT + USER_SLOT_COUNT * D3D12_FRAME_COUNT;

				// displayRT のフォーマット。IRenderTarget には照会手段が無く、Init 時点では
				// 描画先の RT も未確定なため、自前実装の PSO と同じ値を使う (実績あり)。
				static constexpr DXGI_FORMAT RTV_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

				/** デバイスとヒープ */
				ID3D12Device*         g_device  = nullptr;
				ID3D12DescriptorHeap* g_srvHeap = nullptr;
				uint32_t              g_srvSize = 0;

				/** バックエンド区画の空きリスト (末尾から pop する単純なスタック) */
				uint32_t g_freeSlots[BACKEND_SLOT_COUNT] = {};
				uint32_t g_freeCount                     = 0;

				/** バックエンド初期化済みか (Init 失敗時に Shutdown / NewFrame を呼ばないため) */
				bool g_initialized = false;


				D3D12_CPU_DESCRIPTOR_HANDLE SlotCPUHandle(const uint32_t slot)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE handle = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
					handle.ptr += static_cast<SIZE_T>(slot) * g_srvSize;
					return handle;
				}


				D3D12_GPU_DESCRIPTOR_HANDLE SlotGPUHandle(const uint32_t slot)
				{
					D3D12_GPU_DESCRIPTOR_HANDLE handle = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
					handle.ptr += static_cast<UINT64>(slot) * g_srvSize;
					return handle;
				}


				// バックエンドからのディスクリプタ確保要求 (テクスチャ生成時に 1 個ずつ呼ばれる)。
				void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* /*info*/,
				                        D3D12_CPU_DESCRIPTOR_HANDLE* outCPU,
				                        D3D12_GPU_DESCRIPTOR_HANDLE* outGPU)
				{
					if (g_freeCount == 0) {
						EngineAssertMsg(false, "ImGui DX12 バックエンド区画のスロット枯渇");
						*outCPU = {};
						*outGPU = {};
						return;
					}
					const uint32_t slot = g_freeSlots[--g_freeCount];
					*outCPU = SlotCPUHandle(slot);
					*outGPU = SlotGPUHandle(slot);
				}


				// バックエンドからのディスクリプタ解放要求。CPU ハンドルの差分からスロット番号を復元する。
				void SrvDescriptorFree(ImGui_ImplDX12_InitInfo* /*info*/,
				                       D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
				                       D3D12_GPU_DESCRIPTOR_HANDLE /*gpuHandle*/)
				{
					if (!g_srvHeap || g_srvSize == 0) return;
					const SIZE_T base = g_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
					if (cpuHandle.ptr < base) return;
					const uint32_t slot = static_cast<uint32_t>((cpuHandle.ptr - base) / g_srvSize);
					if (slot >= BACKEND_SLOT_COUNT)        return;  // ユーザー区画は空きリストで管理しない
					if (g_freeCount >= BACKEND_SLOT_COUNT) return;
					g_freeSlots[g_freeCount++] = slot;
				}


				// 描画コマンドを無効化する。公式バックエンドはクリップ矩形が潰れたコマンドを
				// ディスクリプタテーブルを張る前にスキップするため、不正なハンドルを渡さずに済む。
				void SkipDrawCmd(ImDrawCmd& cmd)
				{
					cmd.ClipRect  = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
					cmd.ElemCount = 0;
				}


				// ImGui::Image 等でアプリが渡したエンジンのテクスチャを、このフレームの
				// ユーザー区画へコピーして GPU ディスクリプタハンドルへ書き換える。
				// 呼び出しは ImGui_ImplDX12_RenderDrawData より前 (ヒープ設定の後) に行う。
				void ResolveUserTextures(ImDrawData* drawData, ID3D12GraphicsCommandList* cmdList)
				{
					// 自ヒープの GPU レンジ (既に解決済みのハンドルを見分けるため)
					const UINT64 gpuBase = g_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
					const UINT64 gpuEnd  = gpuBase + static_cast<UINT64>(HEAP_SLOT_COUNT) * g_srvSize;

					// このフレームの区画。呼び出し元の BeginFrameIfNeeded のフェンス待ちにより、
					// 同じ区画を前回使った GPU 処理は完了済み。
					const uint32_t frame     = D3D12GraphicsDeviceImpl::GetStaticFrameIndex();
					const uint32_t slotBegin = BACKEND_SLOT_COUNT + frame * USER_SLOT_COUNT;
					const uint32_t slotEnd   = slotBegin + USER_SLOT_COUNT;
					uint32_t       nextSlot  = slotBegin;

					for (int n = 0; n < drawData->CmdListsCount; ++n)
					{
						ImDrawList* drawList = drawData->CmdLists[n];
						for (int c = 0; c < drawList->CmdBuffer.Size; ++c)
						{
							ImDrawCmd& cmd = drawList->CmdBuffer[c];
							if (cmd.UserCallback) continue;
							// _TexData を持つのはバックエンド管理のテクスチャ (フォントアトラス等) で変換不要。
							if (cmd.TexRef._TexData != nullptr) continue;

							const UINT64 texId = static_cast<UINT64>(cmd.TexRef._TexID);
							if (texId == 0) continue;
							if (texId >= gpuBase && texId < gpuEnd) continue;  // 既に自ヒープのハンドル

							if (nextSlot >= slotEnd) {
								// 区画が足りない。不正なハンドルで描かせないよう、このコマンドは捨てる。
								SkipDrawCmd(cmd);
								continue;
							}

							// TODO: 配列 SRV (シャドウ深度マップ等) は公式シェーダが 2D しか読めないため
							//       描かずに警告すべきだが、D3D12SRV に view dimension を問い合わせる
							//       アクセサが無く検出できない (設計書「エディタ全体設計」4.4 節)。
							auto* srv = reinterpret_cast<D3D12SRV*>(texId);
							srv->TransitionToSRV(cmdList);  // PIXEL_SHADER_RESOURCE へ

							const uint32_t slot = nextSlot++;
							g_device->CopyDescriptorsSimple(1, SlotCPUHandle(slot), srv->GetStagingCPUHandle(),
								D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
							cmd.TexRef._TexID = static_cast<ImTextureID>(SlotGPUHandle(slot).ptr);
						}
					}
				}
			} // anonymous namespace


			bool Init()
			{
				g_device   = D3D12GraphicsDeviceImpl::GetStaticDevice();
				auto* impl = static_cast<D3D12GraphicsDeviceImpl*>(GraphicsDevice::Get().GetImplRaw());
				if (!g_device || !impl) return false;

				// shader-visible な SRV ヒープ (バックエンド区画 + フレーム別ユーザー区画)
				D3D12_DESCRIPTOR_HEAP_DESC hd = {};
				hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				hd.NumDescriptors = HEAP_SLOT_COUNT;
				hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				if (FAILED(g_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_srvHeap)))) {
					EngineAssertMsg(false, "ImGui DX12 SRV ヒープ作成失敗");
					return false;
				}
				g_srvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				// 空きリストを降順に積み、pop が slot 0 から返るようにする
				for (uint32_t i = 0; i < BACKEND_SLOT_COUNT; ++i) {
					g_freeSlots[i] = BACKEND_SLOT_COUNT - 1 - i;
				}
				g_freeCount = BACKEND_SLOT_COUNT;

				ImGui_ImplDX12_InitInfo info;  // コンストラクタがゼロ初期化する
				info.Device               = g_device;
				info.CommandQueue         = impl->GetCommandQueue();  // テクスチャのアップロードに使う
				info.NumFramesInFlight    = static_cast<int>(D3D12_FRAME_COUNT);
				info.RTVFormat            = RTV_FORMAT;
				info.DSVFormat            = DXGI_FORMAT_UNKNOWN;      // 深度は使わない
				info.SrvDescriptorHeap    = g_srvHeap;
				info.SrvDescriptorAllocFn = SrvDescriptorAlloc;
				info.SrvDescriptorFreeFn  = SrvDescriptorFree;
				if (!ImGui_ImplDX12_Init(&info)) {
					EngineAssertMsg(false, "ImGui DX12 バックエンド初期化失敗");
					SafeReleaseD3D12(g_srvHeap);
					return false;
				}
				g_initialized = true;
				return true;
			}


			void Shutdown()
			{
				// バックエンドのテクスチャを先に解放させる (SrvDescriptorFree 経由でヒープを触る)。
				if (g_initialized) {
					ImGui_ImplDX12_Shutdown();
					g_initialized = false;
				}
				SafeReleaseD3D12(g_srvHeap);
				g_device    = nullptr;
				g_srvSize   = 0;
				g_freeCount = 0;
			}


			void NewFrame()
			{
				if (!g_initialized) return;
				ImGui_ImplDX12_NewFrame();
			}


			void Render(ImDrawData* drawData, ID3D12GraphicsCommandList* cmdList)
			{
				if (!g_initialized || !drawData || !cmdList || !g_srvHeap) return;

				// 公式の RenderDrawData はヒープを設定しない (設定するのは Multi-Viewport の経路だけ)。
				// ユーザーテクスチャの変換先もこのヒープなので、先に張る。
				// ImGui の後にエンジンが自前のヒープを使う描画を積む場合は張り直しが必要
				// (現状 ImGui の後は CopyResource だけ)。
				ID3D12DescriptorHeap* heaps[] = { g_srvHeap };
				cmdList->SetDescriptorHeaps(1, heaps);

				// エンジンのハンドル (D3D12SRV*) をバックエンドが読める GPU ハンドルへ変換する。
				// 頂点が 0 でもフォントアトラスの更新があるため RenderDrawData は常に呼ぶ。
				ResolveUserTextures(drawData, cmdList);

				ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
			}
		}
	}
}
#endif // AQ_IMGUI

#endif // ENGINE_GRAPHICS_D3D12
