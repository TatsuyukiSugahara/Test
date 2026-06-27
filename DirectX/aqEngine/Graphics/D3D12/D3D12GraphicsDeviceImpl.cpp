#include "aq.h"
#include "D3D12Common.h"
#include "D3D12GraphicsDeviceImpl.h"
#include "D3D12RenderContextImpl.h"
#include "D3D12RenderTarget.h"
#include "D3D12DepthMap.h"
#include "D3D12Buffers.h"
#include "D3D12Shader.h"
#include "D3D12Resources.h"
#include "D3D12RootSignature.h"
#include "D3D12PipelineStateCache.h"
#include "D3D12GpuBuffer.h"
#include "Graphics/GraphicsDevice.h"
#include <vector>


namespace aq
{
	namespace graphics
	{
		namespace
		{
			// リソースステート遷移バリアを記録する
			void TransitionBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
			                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource   = resource;
				barrier.Transition.StateBefore = before;
				barrier.Transition.StateAfter  = after;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				list->ResourceBarrier(1, &barrier);
			}

			// API 非依存フォーマット → DXGI (D3D11 層の ToD3D11Format と同じ対応表)
			DXGI_FORMAT ToDXGIFormat(PixelFormat p)
			{
				switch (p) {
					case PixelFormat::R8G8B8A8_Unorm:      return DXGI_FORMAT_R8G8B8A8_UNORM;
					case PixelFormat::R8G8B8A8_Unorm_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
					case PixelFormat::B8G8R8A8_Unorm:      return DXGI_FORMAT_B8G8R8A8_UNORM;
					case PixelFormat::B8G8R8A8_Unorm_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
					case PixelFormat::D24_Unorm_S8_Uint:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
					case PixelFormat::R16G16B16A16_Float:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
					case PixelFormat::R32_Float:           return DXGI_FORMAT_R32_FLOAT;
					case PixelFormat::R32G32B32A32_Float:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
					case PixelFormat::BC1_Unorm:           return DXGI_FORMAT_BC1_UNORM;
					case PixelFormat::BC1_Unorm_SRGB:      return DXGI_FORMAT_BC1_UNORM_SRGB;
					case PixelFormat::BC2_Unorm:           return DXGI_FORMAT_BC2_UNORM;
					case PixelFormat::BC2_Unorm_SRGB:      return DXGI_FORMAT_BC2_UNORM_SRGB;
					case PixelFormat::BC3_Unorm:           return DXGI_FORMAT_BC3_UNORM;
					case PixelFormat::BC3_Unorm_SRGB:      return DXGI_FORMAT_BC3_UNORM_SRGB;
					case PixelFormat::BC4_Unorm:           return DXGI_FORMAT_BC4_UNORM;
					case PixelFormat::BC5_Unorm:           return DXGI_FORMAT_BC5_UNORM;
					case PixelFormat::BC6H_UFloat16:       return DXGI_FORMAT_BC6H_UF16;
					case PixelFormat::BC7_Unorm:           return DXGI_FORMAT_BC7_UNORM;
					case PixelFormat::BC7_Unorm_SRGB:      return DXGI_FORMAT_BC7_UNORM_SRGB;
					default:                               return DXGI_FORMAT_UNKNOWN;
				}
			}
		}


		D3D12GraphicsDeviceImpl::D3D12GraphicsDeviceImpl()
		{
		}


		D3D12GraphicsDeviceImpl::~D3D12GraphicsDeviceImpl()
		{
		}


		bool D3D12GraphicsDeviceImpl::Initialize(NativeWindowHandle window, uint32_t width, uint32_t height)
		{
			if (!CreateDeviceAndQueues()) return false;
			if (!CreateSwapChain(window.handle, width, height)) return false;
			if (!CreateBackBuffers()) return false;
			if (!CreateRtvDsvHeaps()) return false;
			if (!CreateSRVHeaps()) return false;
			if (!CreateMainRenderTargets(width, height)) return false;

			// ルートシグネチャ (グラフィクス + コンピュート) + PSO キャッシュ
			rootSignature_ = std::make_unique<D3D12RootSignature>();
			if (!rootSignature_->Create(device_)) return false;
			if (!rootSignature_->CreateCompute(device_)) return false;
			psoCache_ = std::make_unique<D3D12PipelineStateCache>();

			// DRAW_INDEXED 用 command signature (ExecuteIndirect・GPU 駆動クラスタカリング)
			{
				D3D12_INDIRECT_ARGUMENT_DESC arg = {};
				arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
				D3D12_COMMAND_SIGNATURE_DESC csd = {};
				csd.ByteStride       = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);  // 20 bytes
				csd.NumArgumentDescs = 1;
				csd.pArgumentDescs   = &arg;
				// DRAW_INDEXED のみで root 引数を変えないため root signature は不要 (nullptr)
				HRESULT hr = device_->CreateCommandSignature(&csd, nullptr, IID_PPV_ARGS(&drawIndexedCmdSig_));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 command signature 作成失敗"); return false; }
			}
			return true;
		}


		ID3D12RootSignature* D3D12GraphicsDeviceImpl::GetRootSignature() const
		{
			return rootSignature_ ? rootSignature_->Get() : nullptr;
		}

		ID3D12RootSignature* D3D12GraphicsDeviceImpl::GetComputeRootSignature() const
		{
			return rootSignature_ ? rootSignature_->GetCompute() : nullptr;
		}


		void D3D12GraphicsDeviceImpl::Finalize()
		{
			// GPU が全コマンドを実行し終えてから解放する
			if (commandQueue_ && fence_) WaitForGPU();

			SafeReleaseD3D12(drawIndexedCmdSig_);
			psoCache_.reset();
			rootSignature_.reset();
			offscreenRTs_.clear();
			for (auto& rt : mainRenderTargets_) rt.reset();
			for (auto& bb : backBuffers_)       SafeReleaseD3D12(bb);
			for (auto& rb : readbackBuf_)       SafeReleaseD3D12(rb);

			SafeReleaseD3D12(srvShaderHeap_);
			SafeReleaseD3D12(srvStagingHeap_);
			SafeReleaseD3D12(dsvHeap_);
			SafeReleaseD3D12(rtvHeap_);
			SafeReleaseD3D12(fence_);
			if (fenceEvent_)
			{
				CloseHandle(static_cast<HANDLE>(fenceEvent_));
				fenceEvent_ = nullptr;
			}
			SafeReleaseD3D12(commandList_);
			for (auto& a : commandAlloc_) SafeReleaseD3D12(a);
			SafeReleaseD3D12(swapChain_);
			SafeReleaseD3D12(commandQueue_);
			SafeReleaseD3D12(device_);
		}


		bool D3D12GraphicsDeviceImpl::CreateDeviceAndQueues()
		{
#ifdef _DEBUG
			// デバッグレイヤを有効化 (失敗しても続行)
			{
				ID3D12Debug* debug = nullptr;
				if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
				{
					debug->EnableDebugLayer();
					debug->Release();
				}
			}
#endif
			HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
			if (FAILED(hr))
			{
				EngineAssertMsg(false, "D3D12 デバイス作成失敗");
				return false;
			}

#ifdef _DEBUG
			// デバッグレイヤの無害な警告を抑制する。
			// 最適化クリア値を nullptr で生成しているため CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE が
			// 毎フレーム大量に出る。これは性能のみの警告でクリア結果は正しいため除外する。
			{
				ID3D12InfoQueue* infoQueue = nullptr;
				if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))))
				{
					D3D12_MESSAGE_ID denyIds[] = {
						D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
						D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
					};
					D3D12_INFO_QUEUE_FILTER filter = {};
					filter.DenyList.NumIDs  = _countof(denyIds);
					filter.DenyList.pIDList = denyIds;
					infoQueue->AddStorageFilterEntries(&filter);
					infoQueue->Release();
				}
			}
#endif

			// コマンドキュー (DIRECT)
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
			queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 コマンドキュー作成失敗"); return false; }

			// コマンドアロケータ (frames-in-flight 分) + コマンドリスト 1 本。
			// コマンドリストは Submit 後すぐ Reset できるが、アロケータは GPU 実行完了まで Reset 不可。
			// よってアロケータをフレーム数だけ用意し、フェンスで使い回しを世代管理する。
			static_assert(FRAME_COUNT == D3D12_FRAME_COUNT, "FRAME_COUNT 不一致");
			for (uint32_t i = 0; i < FRAME_COUNT; ++i)
			{
				hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAlloc_[i]));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 コマンドアロケータ作成失敗"); return false; }
			}

			hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAlloc_[0], nullptr,
			                                IID_PPV_ARGS(&commandList_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 コマンドリスト作成失敗"); return false; }
			commandList_->Close();  // 記録は Present 時に Reset してから行う

			// フェンス + イベント
			hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 フェンス作成失敗"); return false; }
			fenceValue_ = 0;
			fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (!fenceEvent_) { EngineAssertMsg(false, "D3D12 フェンスイベント作成失敗"); return false; }

			return true;
		}


		bool D3D12GraphicsDeviceImpl::CreateSwapChain(void* hwnd, uint32_t width, uint32_t height)
		{
			IDXGIFactory4* factory = nullptr;
			uint32_t flags = 0;
#ifdef _DEBUG
			flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
			HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory));
			if (FAILED(hr)) { EngineAssertMsg(false, "DXGI ファクトリ作成失敗"); return false; }

			DXGI_SWAP_CHAIN_DESC1 desc = {};
			desc.BufferCount      = RENDER_TARGET_COUNT;
			desc.Width            = width;
			desc.Height           = height;
			desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			desc.SampleDesc.Count = 1;

			IDXGISwapChain1* swapChain1 = nullptr;
			hr = factory->CreateSwapChainForHwnd(commandQueue_, static_cast<HWND>(hwnd),
			                                     &desc, nullptr, nullptr, &swapChain1);
			if (FAILED(hr))
			{
				EngineAssertMsg(false, "D3D12 スワップチェーン作成失敗");
				factory->Release();
				return false;
			}

			hr = swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain_));
			swapChain1->Release();
			factory->Release();
			if (FAILED(hr)) { EngineAssertMsg(false, "IDXGISwapChain3 取得失敗"); return false; }

			currentBackBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
			return true;
		}


		bool D3D12GraphicsDeviceImpl::CreateBackBuffers()
		{
			// バックバッファ資源を取得する。これらは描画先ではなく CopyToBackBuffer の複写先。
			// RTV は不要 (CopyResource で書き込む)。初期ステートは PRESENT。
			for (uint32_t i = 0; i < RENDER_TARGET_COUNT; ++i)
			{
				HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 バックバッファ取得失敗"); return false; }
				backBufferStates_[i] = D3D12_RESOURCE_STATE_PRESENT;
			}
			return true;
		}


		bool D3D12GraphicsDeviceImpl::CreateRtvDsvHeaps()
		{
			// カラー RTV ヒープ (メイン RT 2 + オフスクリーン RT 群: GBuffer/ブルームチェーン等)
			rtvCapacity_       = 256;
			rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.NumDescriptors = rtvCapacity_;
				desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvHeap_));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 RTV ヒープ作成失敗"); return false; }
			}

			// 深度 DSV ヒープ (メイン RT 深度 2 + オフスクリーン深度 + シャドウ配列スライス)
			dsvCapacity_       = 64;
			dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.NumDescriptors = dsvCapacity_;
				desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
				desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&dsvHeap_));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 DSV ヒープ作成失敗"); return false; }
			}
			rtvNext_ = 0;
			dsvNext_ = 0;
			return true;
		}


		bool D3D12GraphicsDeviceImpl::CreateMainRenderTargets(uint32_t width, uint32_t height)
		{
			// メイン RT は深度付きオフスクリーン (D3D11 backend と同じ。最後に CopyToBackBuffer)。
			for (uint32_t i = 0; i < RENDER_TARGET_COUNT; ++i)
			{
				const uint32_t rtvIdx = rtvNext_++;
				const uint32_t dsvIdx = dsvNext_++;
				const uint32_t srvIdx = srvStagingNext_++;
				const uint32_t uavIdx = srvStagingNext_++;

				D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
				rtv.ptr += static_cast<SIZE_T>(rtvIdx) * rtvDescriptorSize_;
				D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
				dsv.ptr += static_cast<SIZE_T>(dsvIdx) * dsvDescriptorSize_;
				D3D12_CPU_DESCRIPTOR_HANDLE srv = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				srv.ptr += static_cast<SIZE_T>(srvIdx) * srvDescriptorSize_;
				D3D12_CPU_DESCRIPTOR_HANDLE uav = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				uav.ptr += static_cast<SIZE_T>(uavIdx) * srvDescriptorSize_;

				mainRenderTargets_[i] = std::make_unique<D3D12RenderTarget>();
				// メイン RT は HDR (R16G16B16A16_FLOAT)。PBR ライティングの 1.0 超の値をクランプせず保持し、
				// ポストプロセス(Bloom 合成)でトーンマップして LDR バックバッファへ出力する。
				if (!mainRenderTargets_[i]->CreateOffscreen(device_, width, height,
				        DXGI_FORMAT_R16G16B16A16_FLOAT, /*hasDepth*/true, rtv, dsv, srv, uav))
				{
					return false;
				}
			}
			return true;
		}


		bool D3D12GraphicsDeviceImpl::CreateSRVHeaps()
		{
			srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			// ステージングヒープ (CPU 可視・非 shader-visible)。
			// テクスチャ SRV をテクスチャ寿命の間恒久格納する + 末尾に null SRV を 1 枚。
			constexpr uint32_t STAGING_CAPACITY = 4096;
			srvStagingCapacity_ = STAGING_CAPACITY;
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.NumDescriptors = STAGING_CAPACITY;
				desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvStagingHeap_));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 SRV ステージングヒープ作成失敗"); return false; }
			}

			// シェーダー可視 ring ヒープ。frames-in-flight 分の区画に分け、各フレームは自区画を使う。
			// GPU が前フレームの区画を読んでいる間に上書きしないよう、区画をフレームごとに分離する。
			constexpr uint32_t MAX_TABLES_PER_FRAME = 4096;
			srvRegionSize_         = MAX_TABLES_PER_FRAME * D3D12RootSignature::SRV_TABLE_SIZE;
			srvShaderRingCapacity_ = srvRegionSize_ * FRAME_COUNT;
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.NumDescriptors = srvShaderRingCapacity_;
				desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvShaderHeap_));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 SRV シェーダー可視ヒープ作成失敗"); return false; }
			}

			// null SRV / null UAV をステージング末尾スロットに作成 (未バインドのテーブルスロットを埋める)。
			nullSRVIndex_   = srvStagingCapacity_ - 1;
			nullUAVIndex_   = srvStagingCapacity_ - 2;
			srvStagingNext_ = 0;
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
				nullDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
				nullDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
				nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				nullDesc.Texture2D.MipLevels     = 1;
				D3D12_CPU_DESCRIPTOR_HANDLE h = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				h.ptr += static_cast<SIZE_T>(nullSRVIndex_) * srvDescriptorSize_;
				device_->CreateShaderResourceView(nullptr, &nullDesc, h);
			}
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav = {};
				nullUav.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
				nullUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				D3D12_CPU_DESCRIPTOR_HANDLE h = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				h.ptr += static_cast<SIZE_T>(nullUAVIndex_) * srvDescriptorSize_;
				device_->CreateUnorderedAccessView(nullptr, nullptr, &nullUav, h);
			}
			return true;
		}


		bool D3D12GraphicsDeviceImpl::AllocateSRVTableRange(uint32_t count, uint32_t& outBaseIndex)
		{
			if (count == 0 || count > srvRegionSize_) return false;
			// 現在フレームの区画 [frameIndex_*region, (frameIndex_+1)*region) 内で bump 確保する。
			const uint32_t regionBegin = frameIndex_ * srvRegionSize_;
			const uint32_t regionEnd   = regionBegin + srvRegionSize_;
			if (srvShaderRingNext_ + count > regionEnd)
			{
				EngineAssertMsg(false, "D3D12 SRV ring 区画超過。MAX_TABLES_PER_FRAME を増やす必要あり");
				srvShaderRingNext_ = regionBegin;
			}
			outBaseIndex = srvShaderRingNext_;
			srvShaderRingNext_ += count;
			return true;
		}


		void D3D12GraphicsDeviceImpl::SetupRenderContext(RenderContext& outContext)
		{
			outContext.SetImpl(std::make_unique<D3D12RenderContextImpl>(this));
		}


		void D3D12GraphicsDeviceImpl::BeginFrameIfNeeded()
		{
			if (frameOpen_) return;

			// この frames-in-flight スロットを前回使ったフレームの GPU 処理が終わるまで待つ。
			// (CPU が GPU より FRAME_COUNT フレーム以上先行したときだけブロックする)
			if (fence_->GetCompletedValue() < frameFenceValue_[frameIndex_])
			{
				fence_->SetEventOnCompletion(frameFenceValue_[frameIndex_], static_cast<HANDLE>(fenceEvent_));
				WaitForSingleObject(static_cast<HANDLE>(fenceEvent_), INFINITE);
			}

			currentBackBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

			// このフレームスロットのアロケータを Reset (GPU はもう使っていない)。コマンドリストは 1 本を使い回す。
			commandAlloc_[frameIndex_]->Reset();
			commandList_->Reset(commandAlloc_[frameIndex_], nullptr);

			// バックバッファのバインド/クリアはしない。シーンはメイン RT(オフスクリーン)へ描画し、
			// Present 前に CopyToBackBuffer でバックバッファへ複写する。
			// RT/DSV のバインドとクリアは RenderContext の OMSetRenderTargets/ClearRenderTargetView が行う。

			// shader-visible SRV ヒープをバインドし、このフレームの区画先頭へ巻き戻す。
			// SetGraphicsRootDescriptorTable より前に SetDescriptorHeaps が必要。
			if (srvShaderHeap_)
			{
				ID3D12DescriptorHeap* heaps[] = { srvShaderHeap_ };
				commandList_->SetDescriptorHeaps(1, heaps);
				srvShaderRingNext_ = frameIndex_ * srvRegionSize_;
			}

			// ルートシグネチャを設定 (CBV ルートディスクリプタ / SRV テーブルのバインドに必要)
			if (rootSignature_ && rootSignature_->Get())
				commandList_->SetGraphicsRootSignature(rootSignature_->Get());

			frameOpen_ = true;
			++frameGeneration_;  // RenderContext がフレーム先頭で SRV テーブルを張り直す契機
		}


		void D3D12GraphicsDeviceImpl::SetupDefaultRenderState(RenderContext& /*context*/)
		{
			// Phase 0: ラスタライザ等は PSO に内包されるため、ここでは何もしない。
		}


		uint32_t D3D12GraphicsDeviceImpl::GetMainRenderTargetCount() const
		{
			return RENDER_TARGET_COUNT;
		}


		IRenderTarget& D3D12GraphicsDeviceImpl::GetMainRenderTarget(uint32_t index)
		{
			EngineAssert(index < RENDER_TARGET_COUNT);
			if (index >= RENDER_TARGET_COUNT) index = 0;
			return *mainRenderTargets_[index];
		}


		IRenderTarget* D3D12GraphicsDeviceImpl::GetRenderTarget(uint32_t index)
		{
			// index < RENDER_TARGET_COUNT はメイン RT、それ以上はオフスクリーン RT。
			// (CreateOffscreenRenderTarget が RENDER_TARGET_COUNT + listIndex を返すのに対応)
			if (index < RENDER_TARGET_COUNT) return mainRenderTargets_[index].get();
			const uint32_t off = index - RENDER_TARGET_COUNT;
			if (off < offscreenRTs_.size()) return offscreenRTs_[off].get();
			return nullptr;
		}


		void D3D12GraphicsDeviceImpl::CopyToBackBuffer(IRenderTarget& src)
		{
			BeginFrameIfNeeded();
			auto& rt = static_cast<D3D12RenderTarget&>(src);
			ID3D12Resource* srcColor = rt.GetResource();
			ID3D12Resource* backBuf  = backBuffers_[currentBackBufferIndex_];
			if (!srcColor || !backBuf) return;

			// src(メインRT) → COPY_SOURCE、バックバッファ → COPY_DEST
			if (rt.GetState() != D3D12_RESOURCE_STATE_COPY_SOURCE)
			{
				TransitionBarrier(commandList_, srcColor, rt.GetState(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				rt.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
			}
			TransitionBarrier(commandList_, backBuf,
			                  static_cast<D3D12_RESOURCE_STATES>(backBufferStates_[currentBackBufferIndex_]),
			                  D3D12_RESOURCE_STATE_COPY_DEST);
			backBufferStates_[currentBackBufferIndex_] = D3D12_RESOURCE_STATE_COPY_DEST;

			commandList_->CopyResource(backBuf, srcColor);
		}


		void D3D12GraphicsDeviceImpl::Present()
		{
			BeginFrameIfNeeded();

			// バックバッファを PRESENT へ戻す (CopyToBackBuffer が COPY_DEST にした想定。未コピーでも安全に遷移)
			ID3D12Resource* backBuf = backBuffers_[currentBackBufferIndex_];
			const D3D12_RESOURCE_STATES cur = static_cast<D3D12_RESOURCE_STATES>(backBufferStates_[currentBackBufferIndex_]);
			if (cur != D3D12_RESOURCE_STATE_PRESENT)
			{
				TransitionBarrier(commandList_, backBuf, cur, D3D12_RESOURCE_STATE_PRESENT);
				backBufferStates_[currentBackBufferIndex_] = D3D12_RESOURCE_STATE_PRESENT;
			}

			commandList_->Close();
			ID3D12CommandList* lists[] = { commandList_ };
			commandQueue_->ExecuteCommandLists(1, lists);

			swapChain_->Present(vsyncEnabled_ ? 1 : 0, 0);

			// frames-in-flight: GPU 完了を待たずに次フレームへ進む。
			// このフレームの完了印をフェンスに刻み、次にこのスロットを再利用する時 (BeginFrame) に待つ。
			commandQueue_->Signal(fence_, ++fenceValue_);
			frameFenceValue_[frameIndex_] = fenceValue_;
			// このフレームで Hi-Z リードバックコピーを記録していれば、その完了フェンス値を確定する。
			if (readbackJustWrote_ >= 0)
			{
				readbackFence_[readbackJustWrote_] = fenceValue_;
				readbackJustWrote_                 = -1;
				++dbgReadbackStamps_;
			}
			frameIndex_ = (frameIndex_ + 1) % FRAME_COUNT;

			frameOpen_ = false;
		}


		void D3D12GraphicsDeviceImpl::WaitForGPU()
		{
			const uint64_t value = ++fenceValue_;
			commandQueue_->Signal(fence_, value);
			if (fence_->GetCompletedValue() < value)
			{
				fence_->SetEventOnCompletion(value, static_cast<HANDLE>(fenceEvent_));
				WaitForSingleObject(static_cast<HANDLE>(fenceEvent_), INFINITE);
			}
		}


		bool D3D12GraphicsDeviceImpl::ReadbackOffscreenR32(uint32_t rtIndex, uint32_t width, uint32_t height,
		                                                   std::vector<float>& outData)
		{
			if (width == 0 || height == 0 || !device_) return false;
			IRenderTarget* irt = GetRenderTarget(rtIndex);
			if (!irt) return false;
			auto* rt = static_cast<D3D12RenderTarget*>(irt);
			ID3D12Resource* src = rt->GetResource();
			if (!src) return false;

			// コピー可能フットプリント (行ピッチは 256 アライン)
			D3D12_RESOURCE_DESC srcDesc = src->GetDesc();
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
			UINT   numRows      = 0;
			UINT64 rowSizeBytes = 0;
			UINT64 totalBytes   = 0;
			device_->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes);

			// バッファ未生成 or サイズ変更時に (再)生成
			if (readbackBuf_[0] == nullptr || readbackW_ != width || readbackH_ != height)
			{
				WaitForGPU();  // 進行中のコピー完了を待ってから差し替える
				for (uint32_t i = 0; i < READBACK_SLOTS; ++i)
				{
					SafeReleaseD3D12(readbackBuf_[i]);
					readbackValid_[i] = false;
					readbackFence_[i] = 0;
				}
				readbackWriteIdx_  = 0;
				readbackJustWrote_ = -1;

				D3D12_HEAP_PROPERTIES rbHeap = {};
				rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
				D3D12_RESOURCE_DESC bd = {};
				bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
				bd.Width            = totalBytes;
				bd.Height           = 1;
				bd.DepthOrArraySize = 1;
				bd.MipLevels        = 1;
				bd.Format           = DXGI_FORMAT_UNKNOWN;
				bd.SampleDesc.Count = 1;
				bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				for (uint32_t i = 0; i < READBACK_SLOTS; ++i)
				{
					HRESULT hr = device_->CreateCommittedResource(
						&rbHeap, D3D12_HEAP_FLAG_NONE, &bd,
						D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuf_[i]));
					if (FAILED(hr)) { ++dbgReadbackCreateFails_; return false; }
				}
				readbackW_        = width;
				readbackH_        = height;
				readbackRowPitch_ = footprint.Footprint.RowPitch;
			}

			// 1) GPU 完了済みの最新スロットから CPU へ読む
			bool got = false;
			const uint64_t completed = fence_->GetCompletedValue();
			uint32_t bestSlot = READBACK_SLOTS;
			uint64_t bestFence = 0;
			for (uint32_t i = 0; i < READBACK_SLOTS; ++i)
			{
				if (readbackValid_[i] && readbackFence_[i] != 0 &&
				    readbackFence_[i] <= completed && readbackFence_[i] >= bestFence)
				{
					bestSlot  = i;
					bestFence = readbackFence_[i];
				}
			}
			if (bestSlot < READBACK_SLOTS)
			{
				++dbgReadbackReadFound_;
				void* mapped = nullptr;
				// read range は nullptr (= 全体を読む可能性) で安全側に。範囲指定はバッファ実サイズと
				// ずれると Map 失敗/UB の恐れがあるため使わない。
				const HRESULT hr = readbackBuf_[bestSlot]->Map(0, nullptr, &mapped);
				dbgReadbackMapHr_ = static_cast<uint32_t>(hr);
				if (SUCCEEDED(hr) && mapped)
				{
					outData.resize(static_cast<size_t>(width) * height);
					const uint8_t* base = static_cast<const uint8_t*>(mapped);
					for (uint32_t y = 0; y < height; ++y)
					{
						const float* row = reinterpret_cast<const float*>(base + static_cast<size_t>(y) * readbackRowPitch_);
						memcpy(&outData[static_cast<size_t>(y) * width], row, sizeof(float) * width);
					}
					D3D12_RANGE noWrite = { 0, 0 };
					readbackBuf_[bestSlot]->Unmap(0, &noWrite);
					got = true;
					++dbgReadbackMaps_;
					readbackValid_[bestSlot] = false;  // 成功時のみ消費
				}
			}

			// 2) 今フレームのコピーを書き込みスロットへ記録
			BeginFrameIfNeeded();
			const D3D12_RESOURCE_STATES prev = rt->GetState();
			if (prev != D3D12_RESOURCE_STATE_COPY_SOURCE)
			{
				TransitionBarrier(commandList_, src, prev, D3D12_RESOURCE_STATE_COPY_SOURCE);
				rt->SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
			}

			const uint32_t slot = readbackWriteIdx_;
			D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
			dstLoc.pResource       = readbackBuf_[slot];
			dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dstLoc.PlacedFootprint = footprint;
			D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
			srcLoc.pResource        = src;
			srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			srcLoc.SubresourceIndex = 0;
			commandList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
			++dbgReadbackCopies_;

			readbackValid_[slot] = true;
			readbackFence_[slot] = 0;     // Present で確定
			readbackJustWrote_   = static_cast<int>(slot);
			readbackWriteIdx_    = (slot + 1) % READBACK_SLOTS;

			return got;
		}


		void D3D12GraphicsDeviceImpl::GetReadbackDebug(uint32_t& copies, uint32_t& maps, uint32_t& createFails,
		                                              uint32_t& stamps, uint32_t& validCount, uint32_t& fenceReady)
		{
			copies      = dbgReadbackCopies_;
			maps        = dbgReadbackMaps_;
			createFails = dbgReadbackReadFound_;   // 流用: 完了スロット発見回数
			stamps      = dbgReadbackStamps_;
			validCount  = dbgReadbackMapHr_;        // 流用: 直近 Map HRESULT

			uint64_t maxF = 0;
			for (uint32_t i = 0; i < READBACK_SLOTS; ++i)
				if (readbackFence_[i] > maxF) maxF = readbackFence_[i];
			fenceReady = (fence_ && maxF > 0 && fence_->GetCompletedValue() >= maxF) ? 1u : 0u;
		}


		std::unique_ptr<IGpuBuffer> D3D12GraphicsDeviceImpl::CreateStructuredBuffer(
			uint32_t stride, uint32_t count, const void* data)
		{
			if (!device_ || stride == 0 || count == 0) return nullptr;
			if (srvStagingNext_ >= nullUAVIndex_) { EngineAssertMsg(false, "D3D12 staging 枯渇"); return nullptr; }

			const uint32_t srvIdx = srvStagingNext_++;
			D3D12_CPU_DESCRIPTOR_HANDLE srvH = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
			srvH.ptr += static_cast<SIZE_T>(srvIdx) * srvDescriptorSize_;

			auto buf = std::make_unique<D3D12GpuBuffer>();
			if (!buf->Create(device_, stride * count, stride, /*srv*/true, /*uav*/false, data, srvH, {}))
				return nullptr;
			return buf;
		}


		std::unique_ptr<IGpuBuffer> D3D12GraphicsDeviceImpl::CreateRawBuffer(
			uint32_t byteSize, bool srv, bool uav, const void* initData)
		{
			if (!device_ || byteSize == 0) return nullptr;
			// 16B 境界へ切り上げ (RAW ビューは 4B 要素、安全側に)
			byteSize = (byteSize + 15u) & ~15u;

			D3D12_CPU_DESCRIPTOR_HANDLE srvH = {};
			D3D12_CPU_DESCRIPTOR_HANDLE uavH = {};
			if (srv)
			{
				if (srvStagingNext_ >= nullUAVIndex_) { EngineAssertMsg(false, "D3D12 staging 枯渇"); return nullptr; }
				const uint32_t i = srvStagingNext_++;
				srvH = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				srvH.ptr += static_cast<SIZE_T>(i) * srvDescriptorSize_;
			}
			if (uav)
			{
				if (srvStagingNext_ >= nullUAVIndex_) { EngineAssertMsg(false, "D3D12 staging 枯渇"); return nullptr; }
				const uint32_t i = srvStagingNext_++;
				uavH = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				uavH.ptr += static_cast<SIZE_T>(i) * srvDescriptorSize_;
			}

			auto buf = std::make_unique<D3D12GpuBuffer>();
			if (!buf->Create(device_, byteSize, /*structuredStride*/0, srv, uav, initData, srvH, uavH))
				return nullptr;
			return buf;
		}


		ID3D12Device* D3D12GraphicsDeviceImpl::GetStaticDevice()
		{
			return static_cast<D3D12GraphicsDeviceImpl*>(
				GraphicsDevice::Get().GetImplRaw()
			)->device_;
		}

		uint32_t D3D12GraphicsDeviceImpl::GetStaticFrameIndex()
		{
			return static_cast<D3D12GraphicsDeviceImpl*>(
				GraphicsDevice::Get().GetImplRaw()
			)->frameIndex_;
		}


		// ── リソースファクトリ ────────────────────────────────────────────────
		std::unique_ptr<IVertexBuffer> D3D12GraphicsDeviceImpl::CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vb = std::make_unique<D3D12VertexBuffer>();
			if (!vb->Create(vertexNum, stride, data)) return nullptr;
			return vb;
		}

		std::unique_ptr<IVertexBuffer> D3D12GraphicsDeviceImpl::CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			// frames-in-flight 競合を避けるため内部で FRAME_COUNT 本リングする動的版を使う
			auto vb = std::make_unique<D3D12VertexBuffer>();
			if (!vb->CreateDynamic(vertexNum, stride, data)) return nullptr;
			return vb;
		}

		std::unique_ptr<IIndexBuffer> D3D12GraphicsDeviceImpl::CreateIndexBuffer(uint32_t indexNum, const void* data)
		{
			auto ib = std::make_unique<D3D12IndexBuffer>();
			if (!ib->Create(indexNum, data)) return nullptr;
			return ib;
		}

		std::unique_ptr<IIndexBuffer> D3D12GraphicsDeviceImpl::CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data)
		{
			auto ib = std::make_unique<D3D12IndexBuffer>();
			if (!ib->CreateDynamic(indexNum, format, data)) return nullptr;
			return ib;
		}

		std::unique_ptr<IConstantBuffer> D3D12GraphicsDeviceImpl::CreateConstantBuffer(const void* data, uint32_t size)
		{
			auto cb = std::make_unique<D3D12ConstantBuffer>();
			if (!cb->Create(data, size)) return nullptr;
			return cb;
		}

		std::unique_ptr<IShader> D3D12GraphicsDeviceImpl::CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type)
		{
			auto shader = std::make_unique<D3D12Shader>();
			if (!shader->Load(filePath, entryFunc, type)) return nullptr;
			return shader;
		}

		std::unique_ptr<ISamplerState> D3D12GraphicsDeviceImpl::CreateSamplerState(const SamplerDesc& desc)
		{
			// サンプラーはルートシグネチャの静的サンプラー (s0=Clamp, s1=Wrap) を使うため、
			// ここでは SamplerDesc を保持するだけの軽量 wrapper を返す。
			auto ss = std::make_unique<D3D12SamplerState>();
			ss->Create(desc);
			return ss;
		}

		std::unique_ptr<IShaderResourceView> D3D12GraphicsDeviceImpl::CreateTexture2D(const Texture2DDesc& texDesc, const ImageData& imgData)
		{
			const DXGI_FORMAT format = ToDXGIFormat(texDesc.format);
			if (format == DXGI_FORMAT_UNKNOWN) { EngineAssertMsg(false, "D3D12 CreateTexture2D: 未対応フォーマット"); return nullptr; }
			if (srvStagingNext_ >= nullUAVIndex_) { EngineAssertMsg(false, "D3D12 SRV ステージングヒープ枯渇"); return nullptr; }

			const uint32_t mipLevels = texDesc.mipLevels > 0 ? texDesc.mipLevels : 1;
			const uint32_t arraySize = texDesc.arraySize > 0 ? texDesc.arraySize : 1;

			// ── DEFAULT ヒープにテクスチャ本体を作成 ──
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width              = texDesc.width;
			rd.Height             = texDesc.height;
			rd.DepthOrArraySize   = static_cast<UINT16>(arraySize);
			rd.MipLevels          = static_cast<UINT16>(mipLevels);
			rd.Format             = format;
			rd.SampleDesc.Count   = 1;
			rd.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

			D3D12_HEAP_PROPERTIES defaultHeap = {};
			defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

			ID3D12Resource* texResource = nullptr;
			HRESULT hr = device_->CreateCommittedResource(
				&defaultHeap, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texResource));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 テクスチャリソース作成失敗"); return nullptr; }

			// ── サブリソースデータを準備 (D3D11 実装と同じく単一 or 配列) ──
			const uint32_t numSub = (imgData.subresources && imgData.subresourceCount > 0)
				? imgData.subresourceCount : 1;

			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSub);
			std::vector<UINT>     numRows(numSub);
			std::vector<UINT64>   rowSizes(numSub);
			UINT64 totalBytes = 0;
			device_->GetCopyableFootprints(&rd, 0, numSub, 0,
				layouts.data(), numRows.data(), rowSizes.data(), &totalBytes);

			// ── アップロードヒープを確保し、行ピッチを合わせて memcpy ──
			D3D12_HEAP_PROPERTIES uploadHeap = {};
			uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC ubDesc = {};
			ubDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			ubDesc.Width            = totalBytes;
			ubDesc.Height           = 1;
			ubDesc.DepthOrArraySize = 1;
			ubDesc.MipLevels        = 1;
			ubDesc.Format           = DXGI_FORMAT_UNKNOWN;
			ubDesc.SampleDesc.Count = 1;
			ubDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			ID3D12Resource* uploadBuffer = nullptr;
			hr = device_->CreateCommittedResource(
				&uploadHeap, D3D12_HEAP_FLAG_NONE, &ubDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 テクスチャアップロードバッファ作成失敗"); texResource->Release(); return nullptr; }

			uint8_t* mapped = nullptr;
			uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
			for (uint32_t i = 0; i < numSub; ++i)
			{
				const ImageSubresourceData* sub = (imgData.subresources && imgData.subresourceCount > 0)
					? &imgData.subresources[i] : nullptr;
				const uint8_t* srcBase   = sub ? static_cast<const uint8_t*>(sub->pixels)
				                               : static_cast<const uint8_t*>(imgData.pixels);
				const uint32_t srcRowPit = sub ? sub->rowPitch : imgData.rowPitch;
				const uint64_t dstRowPit = layouts[i].Footprint.RowPitch;
				const uint64_t copyBytes = rowSizes[i];

				uint8_t* dstBase = mapped + layouts[i].Offset;
				for (UINT row = 0; row < numRows[i]; ++row)
				{
					std::memcpy(dstBase + row * dstRowPit,
					            srcBase + row * srcRowPit,
					            static_cast<size_t>(copyBytes));
				}
			}
			uploadBuffer->Unmap(0, nullptr);

			// ── 一時コマンドリストでコピー → PIXEL_SHADER_RESOURCE へ遷移 → 実行・待機 ──
			ID3D12CommandAllocator*    uploadAlloc = nullptr;
			ID3D12GraphicsCommandList* uploadList  = nullptr;
			device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAlloc));
			device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAlloc, nullptr, IID_PPV_ARGS(&uploadList));

			for (uint32_t i = 0; i < numSub; ++i)
			{
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource        = texResource;
				dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				dst.SubresourceIndex = i;

				D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
				srcLoc.pResource       = uploadBuffer;
				srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				srcLoc.PlacedFootprint = layouts[i];

				uploadList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
			}
			TransitionBarrier(uploadList, texResource,
			                  D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			uploadList->Close();

			ID3D12CommandList* lists[] = { uploadList };
			commandQueue_->ExecuteCommandLists(1, lists);
			WaitForGPU();  // 同期実行。アップロードバッファを安全に解放できる。

			uploadList->Release();
			uploadAlloc->Release();
			uploadBuffer->Release();

			// ── ステージングヒープに SRV を作成 ──
			const uint32_t slot = srvStagingNext_++;
			D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
			srvCPU.ptr += static_cast<SIZE_T>(slot) * srvDescriptorSize_;

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format                  = format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			if (texDesc.isCubemap)
			{
				srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.TextureCube.MipLevels     = mipLevels;
			}
			else if (arraySize > 1)
			{
				srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				srvDesc.Texture2DArray.MipLevels       = mipLevels;
				srvDesc.Texture2DArray.ArraySize       = arraySize;
			}
			else
			{
				srvDesc.ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = mipLevels;
			}
			device_->CreateShaderResourceView(texResource, &srvDesc, srvCPU);

			auto view = std::make_unique<D3D12Texture2D>();
			view->Bind(texResource, srvCPU);  // texResource の所有権は D3D12Texture2D へ
			return view;
		}

		std::unique_ptr<IDepthMap> D3D12GraphicsDeviceImpl::CreateDepthMap(uint32_t width, uint32_t /*height*/)
		{
			// シャドウ用は正方配列 (D3D11 backend と同じく resolution 指定)。
			const uint32_t resolution = width;

			D3D12_CPU_DESCRIPTOR_HANDLE dsvs[D3D12DepthMap::kArraySize] = {};
			for (uint32_t i = 0; i < D3D12DepthMap::kArraySize; ++i)
			{
				const uint32_t dsvIdx = dsvNext_++;
				dsvs[i] = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
				dsvs[i].ptr += static_cast<SIZE_T>(dsvIdx) * dsvDescriptorSize_;
			}

			const uint32_t arrSrvIdx = srvStagingNext_++;
			D3D12_CPU_DESCRIPTOR_HANDLE arrSrv = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
			arrSrv.ptr += static_cast<SIZE_T>(arrSrvIdx) * srvDescriptorSize_;

			D3D12_CPU_DESCRIPTOR_HANDLE sliceSrvs[D3D12DepthMap::kArraySize] = {};
			for (uint32_t i = 0; i < D3D12DepthMap::kArraySize; ++i)
			{
				const uint32_t idx = srvStagingNext_++;
				sliceSrvs[i] = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
				sliceSrvs[i].ptr += static_cast<SIZE_T>(idx) * srvDescriptorSize_;
			}

			auto dm = std::make_unique<D3D12DepthMap>();
			if (!dm->Create(device_, resolution, dsvs, arrSrv, sliceSrvs)) return nullptr;
			return dm;  // 所有権は呼び出し元 (ShadowRenderer 等) へ
		}


		uint32_t D3D12GraphicsDeviceImpl::CreateOffscreenRenderTarget(uint32_t width, uint32_t height)
		{
			RenderTargetDesc desc;
			desc.width    = width;
			desc.height   = height;
			desc.hasDepth = true;
			return CreateOffscreenRenderTarget(desc);
		}


		uint32_t D3D12GraphicsDeviceImpl::CreateOffscreenRenderTarget(const RenderTargetDesc& desc)
		{
			const uint32_t rtvIdx = rtvNext_++;
			const uint32_t srvIdx = srvStagingNext_++;
			const uint32_t uavIdx = srvStagingNext_++;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
			rtv.ptr += static_cast<SIZE_T>(rtvIdx) * rtvDescriptorSize_;
			D3D12_CPU_DESCRIPTOR_HANDLE srv = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
			srv.ptr += static_cast<SIZE_T>(srvIdx) * srvDescriptorSize_;
			D3D12_CPU_DESCRIPTOR_HANDLE uav = srvStagingHeap_->GetCPUDescriptorHandleForHeapStart();
			uav.ptr += static_cast<SIZE_T>(uavIdx) * srvDescriptorSize_;

			D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
			if (desc.hasDepth)
			{
				const uint32_t dsvIdx = dsvNext_++;
				dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
				dsv.ptr += static_cast<SIZE_T>(dsvIdx) * dsvDescriptorSize_;
			}

			auto rt = std::make_unique<D3D12RenderTarget>();
			if (!rt->CreateOffscreen(device_, desc.width, desc.height,
			        ToDXGIFormat(desc.colorFormat), desc.hasDepth, rtv, dsv, srv, uav))
			{
				return ~0u;
			}
			offscreenRTs_.push_back(std::move(rt));
			return RENDER_TARGET_COUNT + static_cast<uint32_t>(offscreenRTs_.size() - 1);
		}
	}
}
