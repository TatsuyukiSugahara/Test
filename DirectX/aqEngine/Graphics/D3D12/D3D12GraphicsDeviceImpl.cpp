#include "aq.h"
#include "D3D12Common.h"
#include "D3D12GraphicsDeviceImpl.h"
#include "D3D12RenderContextImpl.h"
#include "D3D12RenderTarget.h"
#include "D3D12Buffers.h"
#include "D3D12Shader.h"
#include "D3D12RootSignature.h"
#include "D3D12PipelineStateCache.h"
#include "Graphics/GraphicsDevice.h"


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
			if (!CreateRenderTargets()) return false;

			// ルートシグネチャ + PSO キャッシュ
			rootSignature_ = std::make_unique<D3D12RootSignature>();
			if (!rootSignature_->Create(device_)) return false;
			psoCache_ = std::make_unique<D3D12PipelineStateCache>();
			return true;
		}


		ID3D12RootSignature* D3D12GraphicsDeviceImpl::GetRootSignature() const
		{
			return rootSignature_ ? rootSignature_->Get() : nullptr;
		}


		void D3D12GraphicsDeviceImpl::Finalize()
		{
			// GPU が全コマンドを実行し終えてから解放する
			if (commandQueue_ && fence_) WaitForGPU();

			psoCache_.reset();
			rootSignature_.reset();
			for (auto& rt : mainRenderTargets_) rt.reset();
			for (auto& bb : backBuffers_)       SafeReleaseD3D12(bb);

			SafeReleaseD3D12(rtvHeap_);
			SafeReleaseD3D12(fence_);
			if (fenceEvent_)
			{
				CloseHandle(static_cast<HANDLE>(fenceEvent_));
				fenceEvent_ = nullptr;
			}
			SafeReleaseD3D12(commandList_);
			SafeReleaseD3D12(commandAlloc_);
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

			// コマンドキュー (DIRECT)
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
			queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 コマンドキュー作成失敗"); return false; }

			// コマンドアロケータ + コマンドリスト (Phase 0 は 1 本のみ・同期実行)
			hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAlloc_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 コマンドアロケータ作成失敗"); return false; }

			hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAlloc_, nullptr,
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

			currentRTIndex_ = swapChain_->GetCurrentBackBufferIndex();
			return true;
		}


		bool D3D12GraphicsDeviceImpl::CreateRenderTargets()
		{
			// RTV ディスクリプタヒープ (バックバッファ枚数分)
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.NumDescriptors = RENDER_TARGET_COUNT;
			heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			HRESULT hr = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 RTV ヒープ作成失敗"); return false; }

			rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

			for (uint32_t i = 0; i < RENDER_TARGET_COUNT; ++i)
			{
				hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 バックバッファ取得失敗"); return false; }

				device_->CreateRenderTargetView(backBuffers_[i], nullptr, rtvHandle);

				mainRenderTargets_[i] = std::make_unique<D3D12RenderTarget>();
				mainRenderTargets_[i]->BindBackBuffer(backBuffers_[i], rtvHandle);

				rtvHandle.ptr += rtvDescriptorSize_;
			}
			return true;
		}


		void D3D12GraphicsDeviceImpl::SetupRenderContext(RenderContext& outContext)
		{
			outContext.SetImpl(std::make_unique<D3D12RenderContextImpl>(this));
		}


		void D3D12GraphicsDeviceImpl::BeginFrameIfNeeded()
		{
			if (frameOpen_) return;

			const uint32_t idx = swapChain_->GetCurrentBackBufferIndex();
			currentRTIndex_ = idx;

			commandAlloc_->Reset();
			commandList_->Reset(commandAlloc_, nullptr);

			TransitionBarrier(commandList_, backBuffers_[idx],
			                  D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
			rtvHandle.ptr += static_cast<SIZE_T>(idx) * rtvDescriptorSize_;
			commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

			const float clearColor[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
			commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

			// ルートシグネチャを設定 (CBV ルートディスクリプタ / SRV テーブルのバインドに必要)
			if (rootSignature_ && rootSignature_->Get())
				commandList_->SetGraphicsRootSignature(rootSignature_->Get());

			frameOpen_ = true;
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


		void D3D12GraphicsDeviceImpl::Present()
		{
			// フレームが記録 (BeginFrameIfNeeded) で開かれていなければ、ここでクリアのみ実行する。
			BeginFrameIfNeeded();

			TransitionBarrier(commandList_, backBuffers_[currentRTIndex_],
			                  D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

			commandList_->Close();
			ID3D12CommandList* lists[] = { commandList_ };
			commandQueue_->ExecuteCommandLists(1, lists);

			swapChain_->Present(1, 0);
			WaitForGPU();

			frameOpen_ = false;
		}


		void D3D12GraphicsDeviceImpl::CopyToBackBuffer(IRenderTarget& /*src*/)
		{
			// Phase 0: オフスクリーン RT パイプラインが無いため何もしない。
			// 確定 RT → バックバッファのコピーは Phase 3 で実装する。
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


		ID3D12Device* D3D12GraphicsDeviceImpl::GetStaticDevice()
		{
			return static_cast<D3D12GraphicsDeviceImpl*>(
				GraphicsDevice::Get().GetImplRaw()
			)->device_;
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
			// アップロードヒープ常駐のため静的版と同じ実装で動的更新も可能
			auto vb = std::make_unique<D3D12VertexBuffer>();
			if (!vb->Create(vertexNum, stride, data)) return nullptr;
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

		std::unique_ptr<ISamplerState> D3D12GraphicsDeviceImpl::CreateSamplerState(const SamplerDesc&)
		{
			EngineAssertMsg(false, "D3D12 CreateSamplerState は Phase 2 で実装予定");
			return nullptr;
		}

		std::unique_ptr<IShaderResourceView> D3D12GraphicsDeviceImpl::CreateTexture2D(const Texture2DDesc&, const ImageData&)
		{
			EngineAssertMsg(false, "D3D12 CreateTexture2D は Phase 2 で実装予定");
			return nullptr;
		}

		std::unique_ptr<IDepthMap> D3D12GraphicsDeviceImpl::CreateDepthMap(uint32_t, uint32_t)
		{
			EngineAssertMsg(false, "D3D12 CreateDepthMap は Phase 3 で実装予定");
			return nullptr;
		}
	}
}
