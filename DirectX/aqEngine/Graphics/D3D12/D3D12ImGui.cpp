#include "aq.h"
// ImGui 専用バックエンド。AQ_IMGUI 未定義(Xbox 構成など)では空 TU にする。
#ifdef AQ_IMGUI
#include "D3D12Common.h"
#include "D3D12ImGui.h"
#include "D3D12GraphicsDeviceImpl.h"
#include "D3D12Resources.h"
#include "Graphics/GraphicsDevice.h"
#include <imgui/imgui.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

// ─────────────────────────────────────────────────────────────────────────
// 自前の DirectX12 ImGui レンダラー (Phase 4)。
// 同梱の imgui_impl_dx12 はコア(1.92 WIP 19193)より新しく非互換のため使用せず、
// 現行 API (GetTexDataAsRGBA32 / ImDrawData::CmdLists / ImTextureID=ImU64) で実装する。
// エンジンは Present 毎に WaitForGPU する同期実行のため、VB/IB は単一バッファで安全。
// ─────────────────────────────────────────────────────────────────────────

namespace aq
{
	namespace graphics
	{
		namespace D3D12ImGui
		{
			namespace
			{
				ID3D12Device*            g_device   = nullptr;
				ID3D12CommandQueue*      g_queue    = nullptr;
				ID3D12RootSignature*     g_rootSig  = nullptr;
				ID3D12PipelineState*     g_pso      = nullptr;
				ID3D12DescriptorHeap*    g_srvHeap  = nullptr;   // shader-visible (font + user textures)
				uint32_t                 g_srvSize  = 0;
				ID3D12Resource*          g_fontTex  = nullptr;

				ID3D12Resource*          g_vb       = nullptr;
				ID3D12Resource*          g_ib       = nullptr;
				uint32_t                 g_vbCap    = 0;  // 頂点数
				uint32_t                 g_ibCap    = 0;  // インデックス数

				constexpr uint32_t       HEAP_SIZE  = 64; // slot 0=フォント, 1..=動的(エンジンテクスチャ)
				uint32_t                 g_dynNext  = 1;  // フレーム内の動的スロット割当位置

				constexpr DXGI_FORMAT    RTV_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

				struct VertexConstant { float mvp[4][4]; };

				const char* kShaderHLSL = R"(
cbuffer vertexBuffer : register(b0) { float4x4 ProjectionMatrix; };
struct VS_INPUT { float2 pos:POSITION; float2 uv:TEXCOORD0; float4 col:COLOR0; };
struct PS_INPUT { float4 pos:SV_POSITION; float4 col:COLOR0; float2 uv:TEXCOORD0; };
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT o;
    o.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
    o.col = input.col;
    o.uv  = input.uv;
    return o;
}
Texture2D    tex0    : register(t0);
SamplerState sampler0: register(s0);
float4 PSMain(PS_INPUT input) : SV_Target
{
    return input.col * tex0.Sample(sampler0, input.uv);
}
)";

				bool CreateRootSignature()
				{
					D3D12_DESCRIPTOR_RANGE srvRange = {};
					srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
					srvRange.NumDescriptors     = 1;
					srvRange.BaseShaderRegister = 0;  // t0
					srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

					D3D12_ROOT_PARAMETER params[2] = {};
					// [0] mvp 行列 (ルート定数 16 個)
					params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
					params[0].Constants.ShaderRegister = 0;  // b0
					params[0].Constants.Num32BitValues = 16;
					params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
					// [1] テクスチャ SRV テーブル
					params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					params[1].DescriptorTable.NumDescriptorRanges = 1;
					params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
					params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

					D3D12_STATIC_SAMPLER_DESC samp = {};
					samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
					samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
					samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
					samp.MaxLOD           = D3D12_FLOAT32_MAX;
					samp.ShaderRegister   = 0;  // s0
					samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

					D3D12_ROOT_SIGNATURE_DESC desc = {};
					desc.NumParameters     = 2;
					desc.pParameters       = params;
					desc.NumStaticSamplers = 1;
					desc.pStaticSamplers   = &samp;
					desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

					ID3DBlob* blob = nullptr;
					ID3DBlob* err  = nullptr;
					if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)))
					{
						if (err) { OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer())); err->Release(); }
						return false;
					}
					if (err) err->Release();
					HRESULT hr = g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
					                                            IID_PPV_ARGS(&g_rootSig));
					blob->Release();
					return SUCCEEDED(hr);
				}

				bool CreatePipeline()
				{
					ID3DBlob* vs = nullptr; ID3DBlob* ps = nullptr; ID3DBlob* err = nullptr;
					if (FAILED(D3DCompile(kShaderHLSL, strlen(kShaderHLSL), nullptr, nullptr, nullptr,
					           "VSMain", "vs_5_0", 0, 0, &vs, &err)))
					{
						if (err) { OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer())); err->Release(); }
						return false;
					}
					if (FAILED(D3DCompile(kShaderHLSL, strlen(kShaderHLSL), nullptr, nullptr, nullptr,
					           "PSMain", "ps_5_0", 0, 0, &ps, &err)))
					{
						if (err) { OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer())); err->Release(); }
						vs->Release();
						return false;
					}

					D3D12_INPUT_ELEMENT_DESC layout[] = {
						{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
						{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
						{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
					};

					D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
					pso.pRootSignature        = g_rootSig;
					pso.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
					pso.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
					pso.InputLayout           = { layout, _countof(layout) };
					pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
					pso.NumRenderTargets      = 1;
					pso.RTVFormats[0]         = RTV_FORMAT;
					pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
					pso.SampleMask            = UINT_MAX;
					pso.SampleDesc.Count      = 1;

					// ブレンド (通常アルファ)
					pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
					pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
					pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
					pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
					pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
					pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
					pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
					pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
					// ラスタライザ (カリングなし・シザー有効)
					pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
					pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
					pso.RasterizerState.DepthClipEnable = TRUE;
					// 深度なし
					pso.DepthStencilState.DepthEnable   = FALSE;
					pso.DepthStencilState.StencilEnable = FALSE;

					HRESULT hr = g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_pso));
					vs->Release();
					ps->Release();
					return SUCCEEDED(hr);
				}

				bool CreateFontTexture()
				{
					ImGuiIO& io = ImGui::GetIO();
					unsigned char* pixels = nullptr;
					int width = 0, height = 0;
					io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

					D3D12_HEAP_PROPERTIES defHeap = {}; defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
					D3D12_RESOURCE_DESC td = {};
					td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
					td.Width            = width;
					td.Height           = height;
					td.DepthOrArraySize = 1;
					td.MipLevels        = 1;
					td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
					td.SampleDesc.Count = 1;
					if (FAILED(g_device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &td,
					           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_fontTex))))
						return false;

					// アップロードバッファ (行ピッチ 256 アライン)
					const uint32_t rowPitch = (width * 4 + 255) & ~255u;
					const uint32_t uploadSize = rowPitch * height;
					D3D12_HEAP_PROPERTIES upHeap = {}; upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
					D3D12_RESOURCE_DESC ub = {};
					ub.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
					ub.Width            = uploadSize;
					ub.Height           = 1;
					ub.DepthOrArraySize = 1;
					ub.MipLevels        = 1;
					ub.SampleDesc.Count = 1;
					ub.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
					ID3D12Resource* upload = nullptr;
					if (FAILED(g_device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &ub,
					           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
						return false;

					uint8_t* mapped = nullptr;
					upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
					for (int y = 0; y < height; ++y)
						memcpy(mapped + y * rowPitch, pixels + y * width * 4, width * 4);
					upload->Unmap(0, nullptr);

					// 一時コマンドリストでコピー → PIXEL_SHADER_RESOURCE へ遷移 → 実行・待機
					ID3D12CommandAllocator*    alloc = nullptr;
					ID3D12GraphicsCommandList* list  = nullptr;
					g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
					g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list));

					D3D12_TEXTURE_COPY_LOCATION dst = {};
					dst.pResource = g_fontTex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
					D3D12_TEXTURE_COPY_LOCATION src = {};
					src.pResource = upload; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
					src.PlacedFootprint.Footprint.Width    = width;
					src.PlacedFootprint.Footprint.Height   = height;
					src.PlacedFootprint.Footprint.Depth    = 1;
					src.PlacedFootprint.Footprint.RowPitch = rowPitch;
					list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

					D3D12_RESOURCE_BARRIER b = {};
					b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					b.Transition.pResource   = g_fontTex;
					b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
					b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					list->ResourceBarrier(1, &b);
					list->Close();

					ID3D12CommandList* lists[] = { list };
					g_queue->ExecuteCommandLists(1, lists);

					// フォントアップロード完了を待つ (一時フェンス)
					ID3D12Fence* fence = nullptr;
					g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
					HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
					g_queue->Signal(fence, 1);
					if (fence->GetCompletedValue() < 1) { fence->SetEventOnCompletion(1, ev); WaitForSingleObject(ev, INFINITE); }
					CloseHandle(ev);
					fence->Release();
					list->Release();
					alloc->Release();
					upload->Release();

					// フォント SRV をヒープ先頭 (slot 0) に作成し、TexID に GPU ハンドルを設定
					D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
					D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
					D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
					sv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
					sv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
					sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					sv.Texture2D.MipLevels     = 1;
					g_device->CreateShaderResourceView(g_fontTex, &sv, cpu);
					io.Fonts->SetTexID(static_cast<ImTextureID>(gpu.ptr));
					return true;
				}

				// VB/IB を必要数まで拡張する (容量不足時のみ再生成)
				bool EnsureBuffers(uint32_t vtxCount, uint32_t idxCount)
				{
					D3D12_HEAP_PROPERTIES upHeap = {}; upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
					auto makeBuffer = [&](ID3D12Resource** res, uint32_t bytes) -> bool
					{
						D3D12_RESOURCE_DESC d = {};
						d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
						d.Width            = bytes;
						d.Height           = 1;
						d.DepthOrArraySize = 1;
						d.MipLevels        = 1;
						d.SampleDesc.Count = 1;
						d.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
						return SUCCEEDED(g_device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &d,
						       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(res)));
					};
					// frames-in-flight 分を 1 資源にまとめて確保し、フレームごとの領域を使う。
					if (vtxCount > g_vbCap)
					{
						SafeReleaseD3D12(g_vb);
						g_vbCap = vtxCount + 5000;
						if (!makeBuffer(&g_vb, g_vbCap * sizeof(ImDrawVert) * D3D12_FRAME_COUNT)) return false;
					}
					if (idxCount > g_ibCap)
					{
						SafeReleaseD3D12(g_ib);
						g_ibCap = idxCount + 10000;
						if (!makeBuffer(&g_ib, g_ibCap * sizeof(ImDrawIdx) * D3D12_FRAME_COUNT)) return false;
					}
					return true;
				}
			} // anonymous namespace


			bool Init()
			{
				g_device = D3D12GraphicsDeviceImpl::GetStaticDevice();
				auto* impl = static_cast<D3D12GraphicsDeviceImpl*>(GraphicsDevice::Get().GetImplRaw());
				if (!g_device || !impl) return false;
				g_queue  = impl->GetCommandQueue();

				D3D12_DESCRIPTOR_HEAP_DESC hd = {};
				hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				hd.NumDescriptors = HEAP_SIZE;  // フォント + ユーザーテクスチャ
				hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				if (FAILED(g_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_srvHeap)))) return false;
				g_srvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				ImGuiIO& io = ImGui::GetIO();
				io.BackendRendererName = "imgui_impl_aq_dx12";
				io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

				if (!CreateRootSignature()) { EngineAssertMsg(false, "ImGui DX12 ルートシグネチャ失敗"); return false; }
				if (!CreatePipeline())      { EngineAssertMsg(false, "ImGui DX12 PSO 失敗"); return false; }
				if (!CreateFontTexture())   { EngineAssertMsg(false, "ImGui DX12 フォント失敗"); return false; }
				return true;
			}


			void Shutdown()
			{
				SafeReleaseD3D12(g_vb);
				SafeReleaseD3D12(g_ib);
				SafeReleaseD3D12(g_fontTex);
				SafeReleaseD3D12(g_pso);
				SafeReleaseD3D12(g_rootSig);
				SafeReleaseD3D12(g_srvHeap);
				g_vbCap = g_ibCap = 0;
			}


			void NewFrame() {}  // フォント等は Init で生成済み


			void Render(ImDrawData* drawData, ID3D12GraphicsCommandList* cmdList)
			{
				if (!drawData || !cmdList || !g_pso) return;
				if (drawData->TotalVtxCount <= 0) return;
				if (!EnsureBuffers(drawData->TotalVtxCount, drawData->TotalIdxCount)) return;

				// frames-in-flight: このフレームの VB/IB 領域オフセット
				const uint32_t frame    = D3D12GraphicsDeviceImpl::GetStaticFrameIndex();
				const UINT64   vbOffset = static_cast<UINT64>(frame) * g_vbCap * sizeof(ImDrawVert);
				const UINT64   ibOffset = static_cast<UINT64>(frame) * g_ibCap * sizeof(ImDrawIdx);

				// VB/IB へ全描画リストを連結コピー (現在フレームの領域へ)
				uint8_t* vtxBase = nullptr; uint8_t* idxBase = nullptr;
				g_vb->Map(0, nullptr, reinterpret_cast<void**>(&vtxBase));
				g_ib->Map(0, nullptr, reinterpret_cast<void**>(&idxBase));
				ImDrawVert* vtxDst = reinterpret_cast<ImDrawVert*>(vtxBase + vbOffset);
				ImDrawIdx*  idxDst = reinterpret_cast<ImDrawIdx*>(idxBase + ibOffset);
				for (int n = 0; n < drawData->CmdListsCount; ++n)
				{
					const ImDrawList* cl = drawData->CmdLists[n];
					memcpy(vtxDst, cl->VtxBuffer.Data, cl->VtxBuffer.Size * sizeof(ImDrawVert));
					memcpy(idxDst, cl->IdxBuffer.Data, cl->IdxBuffer.Size * sizeof(ImDrawIdx));
					vtxDst += cl->VtxBuffer.Size;
					idxDst += cl->IdxBuffer.Size;
				}
				g_vb->Unmap(0, nullptr);
				g_ib->Unmap(0, nullptr);

				// レンダーステート設定
				D3D12_VIEWPORT vp = { 0, 0, drawData->DisplaySize.x, drawData->DisplaySize.y, 0.f, 1.f };
				cmdList->RSSetViewports(1, &vp);

				D3D12_VERTEX_BUFFER_VIEW vbv = {};
				vbv.BufferLocation = g_vb->GetGPUVirtualAddress() + vbOffset;
				vbv.SizeInBytes    = g_vbCap * sizeof(ImDrawVert);
				vbv.StrideInBytes  = sizeof(ImDrawVert);
				cmdList->IASetVertexBuffers(0, 1, &vbv);

				D3D12_INDEX_BUFFER_VIEW ibv = {};
				ibv.BufferLocation = g_ib->GetGPUVirtualAddress() + ibOffset;
				ibv.SizeInBytes    = g_ibCap * sizeof(ImDrawIdx);
				ibv.Format         = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
				cmdList->IASetIndexBuffer(&ibv);

				cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				cmdList->SetPipelineState(g_pso);
				cmdList->SetGraphicsRootSignature(g_rootSig);
				ID3D12DescriptorHeap* heaps[] = { g_srvHeap };
				cmdList->SetDescriptorHeaps(1, heaps);

				// 正射影 mvp をルート定数で渡す
				const float L = drawData->DisplayPos.x;
				const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
				const float T = drawData->DisplayPos.y;
				const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;
				VertexConstant vc = {{
					{ 2.0f/(R-L), 0,          0,    0 },
					{ 0,          2.0f/(T-B), 0,    0 },
					{ 0,          0,          0.5f, 0 },
					{ (R+L)/(L-R), (T+B)/(B-T), 0.5f, 1 },
				}};
				cmdList->SetGraphicsRoot32BitConstants(0, 16, &vc, 0);

				const float blendFactor[4] = { 0, 0, 0, 0 };
				cmdList->OMSetBlendFactor(blendFactor);

				// TexID 解決用にヒープの先頭ハンドルと GPU レンジを用意する。
				const D3D12_CPU_DESCRIPTOR_HANDLE heapCpu0 = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
				const D3D12_GPU_DESCRIPTOR_HANDLE heapGpu0 = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
				const UINT64 gpuLo = heapGpu0.ptr;
				const UINT64 gpuHi = heapGpu0.ptr + static_cast<UINT64>(HEAP_SIZE) * g_srvSize;
				// frames-in-flight: slot 0=フォント、残りを FRAME_COUNT 区画に分け、
				// このフレームの区画だけ使う (GPUが前フレームのディスクリプタを読む間の上書き防止)。
				const uint32_t dynRegion  = (HEAP_SIZE - 1) / D3D12_FRAME_COUNT;
				const uint32_t dynBegin   = 1 + D3D12GraphicsDeviceImpl::GetStaticFrameIndex() * dynRegion;
				const uint32_t dynEnd     = dynBegin + dynRegion;
				g_dynNext = dynBegin;

				// 描画コマンドを発行
				const ImVec2 clipOff = drawData->DisplayPos;
				int globalVtx = 0, globalIdx = 0;
				for (int n = 0; n < drawData->CmdListsCount; ++n)
				{
					const ImDrawList* cl = drawData->CmdLists[n];
					for (int c = 0; c < cl->CmdBuffer.Size; ++c)
					{
						const ImDrawCmd& cmd = cl->CmdBuffer[c];
						if (cmd.UserCallback) { cmd.UserCallback(cl, &cmd); continue; }

						D3D12_RECT scissor = {
							(LONG)(cmd.ClipRect.x - clipOff.x), (LONG)(cmd.ClipRect.y - clipOff.y),
							(LONG)(cmd.ClipRect.z - clipOff.x), (LONG)(cmd.ClipRect.w - clipOff.y) };
						if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;
						cmdList->RSSetScissorRects(1, &scissor);

						// TexID 解決:
						//  - フォント等で既に自ヒープの GPU ハンドルならそのまま使う。
						//  - エンジンのテクスチャは GetNativeHandle() が D3D12SRV* を返すため、
						//    その staging SRV を動的スロットへコピーして GPU ハンドル化する。
						const UINT64 id = static_cast<UINT64>(cmd.GetTexID());
						D3D12_GPU_DESCRIPTOR_HANDLE tex = {};
						if (id >= gpuLo && id < gpuHi)
						{
							tex.ptr = id;
						}
						else if (id != 0 && g_dynNext < dynEnd)
						{
							auto* srv = reinterpret_cast<D3D12SRV*>(id);
							srv->TransitionToSRV(cmdList);  // PIXEL_SHADER_RESOURCE へ
							const uint32_t slot = g_dynNext++;
							D3D12_CPU_DESCRIPTOR_HANDLE dst = heapCpu0;
							dst.ptr += static_cast<SIZE_T>(slot) * g_srvSize;
							g_device->CopyDescriptorsSimple(1, dst, srv->GetStagingCPUHandle(),
								D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
							tex = heapGpu0;
							tex.ptr += static_cast<UINT64>(slot) * g_srvSize;
						}
						else
						{
							tex.ptr = gpuLo;  // フォールバック (フォント) でクラッシュ回避
						}
						cmdList->SetGraphicsRootDescriptorTable(1, tex);

						cmdList->DrawIndexedInstanced(cmd.ElemCount, 1,
							cmd.IdxOffset + globalIdx, cmd.VtxOffset + globalVtx, 0);
					}
					globalVtx += cl->VtxBuffer.Size;
					globalIdx += cl->IdxBuffer.Size;
				}
			}
		}
	}
}
#endif // AQ_IMGUI
