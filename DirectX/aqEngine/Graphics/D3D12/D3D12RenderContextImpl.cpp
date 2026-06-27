#include "aq.h"
#include "D3D12Common.h"
#include "D3D12RenderContextImpl.h"
#include "D3D12GraphicsDeviceImpl.h"
#include "D3D12Buffers.h"
#include "D3D12Shader.h"
#include "D3D12Resources.h"
#include "D3D12RenderTarget.h"
#include "D3D12DepthMap.h"
#include "D3D12PipelineStateCache.h"
#include "D3D12RootSignature.h"
#include "D3D12GpuBuffer.h"

// ─────────────────────────────────────────────────────────────────────────
// Phase 1b: コマンドリスト記録。DX11 のイミディエイト設定を「保留ステート + Draw 時 flush」
// モデルで DX12 PSO / ルート引数に変換する。詳細は D3D12Backend設計.md §3〜§5。
// 未実装 (P2 以降): テクスチャ SRV テーブル / サンプラーヒープ / コンピュート / オフスクリーン RT。
// ─────────────────────────────────────────────────────────────────────────

namespace aq
{
	namespace graphics
	{
		namespace
		{
			D3D12_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology topo)
			{
				switch (topo)
				{
					case PrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
					case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
					case PrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
					case PrimitiveTopology::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
					case PrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
				}
				return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			}

			D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopoType(PrimitiveTopology topo)
			{
				switch (topo)
				{
					case PrimitiveTopology::TriangleList:
					case PrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
					case PrimitiveTopology::LineList:
					case PrimitiveTopology::LineStrip:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
					case PrimitiveTopology::PointList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
				}
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			}

			// RT カラー資源を指定ステートへ遷移する (現在ステートと異なる場合のみ)
			void TransitionRT(ID3D12GraphicsCommandList* list, D3D12RenderTarget& rt, D3D12_RESOURCE_STATES after)
			{
				if (!rt.GetResource() || rt.GetState() == after) return;
				D3D12_RESOURCE_BARRIER b = {};
				b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource   = rt.GetResource();
				b.Transition.StateBefore = rt.GetState();
				b.Transition.StateAfter  = after;
				b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				list->ResourceBarrier(1, &b);
				rt.SetState(after);
			}
		}


		D3D12RenderContextImpl::D3D12RenderContextImpl(D3D12GraphicsDeviceImpl* device)
			: device_(device)
		{
		}


		void D3D12RenderContextImpl::EnsureGraphicsRootSig()
		{
			if (!graphicsRootDirty_) return;
			device_->GetCommandList()->SetGraphicsRootSignature(device_->GetRootSignature());
			graphicsRootDirty_ = false;
			srvDirty_          = true;  // root sig 変更でテーブルバインドが失われるため張り直す
		}


		void D3D12RenderContextImpl::FlushPipeline()
		{
			EnsureGraphicsRootSig();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();
			D3D12PipelineStateCache*   cache = device_->GetPipelineCache();
			auto* vs = static_cast<D3D12Shader*>(pendingVS_);
			auto* ps = static_cast<D3D12Shader*>(pendingPS_);
			if (!list || !cache || !vs) return;

			D3D12PipelineStateCache::Key key;
			key.vs       = pendingVS_;
			key.ps       = pendingPS_;
			key.blend    = blend_;
			key.depth    = depth_;
			key.topoType = ToTopoType(topology_);
			key.rtCount  = rtvCount_;
			for (uint32_t i = 0; i < MAX_RTV; ++i)
				key.rtFormats[i] = (i < rtvCount_) ? rtFormats_[i] : DXGI_FORMAT_UNKNOWN;
			key.dsFormat = hasDSV_ ? dsFormat_ : DXGI_FORMAT_UNKNOWN;

			ID3D12PipelineState* pso = cache->GetOrCreate(
				D3D12GraphicsDeviceImpl::GetStaticDevice(), device_->GetRootSignature(), vs, ps, key);
			if (pso) list->SetPipelineState(pso);
			list->IASetPrimitiveTopology(ToD3DTopology(topology_));
		}


		void D3D12RenderContextImpl::FlushDescriptors()
		{
			static_assert(SRV_SLOT_COUNT == D3D12RootSignature::SRV_TABLE_SIZE,
			              "保留 SRV スロット数とルートシグネチャの SRV テーブルサイズが不一致");

			// 新フレームではコマンドリスト Reset でテーブルバインドが失われるため強制的に張り直す。
			const uint64_t gen = device_->GetFrameGeneration();
			if (gen != lastFrameGen_) { srvDirty_ = true; lastFrameGen_ = gen; }

			if (!srvDirty_) return;  // 前回 Draw のテーブルバインドがそのまま有効

			ID3D12Device*              dev  = D3D12GraphicsDeviceImpl::GetStaticDevice();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();
			ID3D12DescriptorHeap*      heap = device_->GetSRVShaderHeap();
			if (!dev || !list || !heap) return;

			// ring から SRV_SLOT_COUNT 個の連続スロットを確保
			uint32_t base = 0;
			if (!device_->AllocateSRVTableRange(SRV_SLOT_COUNT, base)) return;

			const uint32_t size = device_->GetSRVDescriptorSize();
			D3D12_CPU_DESCRIPTOR_HANDLE dstCPU = heap->GetCPUDescriptorHandleForHeapStart();
			dstCPU.ptr += static_cast<SIZE_T>(base) * size;
			D3D12_GPU_DESCRIPTOR_HANDLE dstGPU = heap->GetGPUDescriptorHandleForHeapStart();
			dstGPU.ptr += static_cast<UINT64>(base) * size;

			// null SRV のソースハンドル (未バインドスロットを埋める)
			D3D12_CPU_DESCRIPTOR_HANDLE nullSrc = device_->GetSRVStagingHeap()->GetCPUDescriptorHandleForHeapStart();
			nullSrc.ptr += static_cast<SIZE_T>(device_->GetNullSRVIndex()) * size;

			for (uint32_t i = 0; i < SRV_SLOT_COUNT; ++i)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE slotDst = dstCPU;
				slotDst.ptr += static_cast<SIZE_T>(i) * size;
				const D3D12_CPU_DESCRIPTOR_HANDLE src = pendingSRV_[i].ptr ? pendingSRV_[i] : nullSrc;
				dev->CopyDescriptorsSimple(1, slotDst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}

			list->SetGraphicsRootDescriptorTable(D3D12RootSignature::PARAM_SRV_TABLE, dstGPU);
			srvDirty_ = false;
		}


		// ── 出力マージャ / ラスタライザ ──────────────────────────────────────
		void D3D12RenderContextImpl::ApplyRenderTargets()
		{
			ID3D12GraphicsCommandList* list = device_->GetCommandList();
			list->OMSetRenderTargets(rtvCount_, rtvCount_ ? rtvHandles_ : nullptr, FALSE,
			                         hasDSV_ ? &dsvHandle_ : nullptr);
		}

		void D3D12RenderContextImpl::OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget)
		{
			device_->BeginFrameIfNeeded();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();

			rtvCount_ = 0;
			hasDSV_   = false;
			if (renderTarget && numViews > 0)
			{
				// D3D11 backend 同様、renderTarget は RenderTarget[] の先頭を指す。
				auto* rts = static_cast<D3D12RenderTarget*>(renderTarget);
				for (uint32_t i = 0; i < numViews && i < MAX_RTV; ++i)
				{
					TransitionRT(list, rts[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
					rtvHandles_[i] = rts[i].GetRtvHandle();
					rtFormats_[i]  = rts[i].GetColorFormat();
					++rtvCount_;
				}
				if (rts[0].HasDepth())
				{
					dsvHandle_ = rts[0].GetDsvHandle();
					dsFormat_  = DXGI_FORMAT_D24_UNORM_S8_UINT;  // オフスクリーン深度フォーマット
					hasDSV_    = true;
				}
			}
			ApplyRenderTargets();
		}

		void D3D12RenderContextImpl::OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets)
		{
			device_->BeginFrameIfNeeded();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();

			rtvCount_ = 0;
			hasDSV_   = false;
			for (uint32_t i = 0; i < numViews && i < MAX_RTV; ++i)
			{
				auto* rt = static_cast<D3D12RenderTarget*>(renderTargets[i]);
				if (!rt) continue;
				TransitionRT(list, *rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
				rtvHandles_[rtvCount_] = rt->GetRtvHandle();
				rtFormats_[rtvCount_]  = rt->GetColorFormat();
				++rtvCount_;
			}
			if (numViews > 0 && renderTargets[0])
			{
				auto* rt0 = static_cast<D3D12RenderTarget*>(renderTargets[0]);
				if (rt0->HasDepth())
				{
					dsvHandle_ = rt0->GetDsvHandle();
					dsFormat_  = DXGI_FORMAT_D24_UNORM_S8_UINT;
					hasDSV_    = true;
				}
			}
			ApplyRenderTargets();
		}

		void D3D12RenderContextImpl::OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
		{
			device_->BeginFrameIfNeeded();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();

			auto& color = static_cast<D3D12RenderTarget&>(colorRT);
			auto& depth = static_cast<D3D12RenderTarget&>(depthSourceRT);
			TransitionRT(list, color, D3D12_RESOURCE_STATE_RENDER_TARGET);

			rtvHandles_[0] = color.GetRtvHandle();
			rtFormats_[0]  = color.GetColorFormat();
			rtvCount_      = 1;
			dsvHandle_     = depth.GetDsvHandle();
			dsFormat_      = DXGI_FORMAT_D24_UNORM_S8_UINT;
			hasDSV_        = depth.HasDepth();
			ApplyRenderTargets();
		}

		void D3D12RenderContextImpl::OMSetDepthMode(DepthMode mode) { depth_ = mode; }
		void D3D12RenderContextImpl::OMSetBlendMode(BlendMode mode) { blend_ = mode; }

		void D3D12RenderContextImpl::ClearDepthBuffer()
		{
			device_->BeginFrameIfNeeded();
			if (hasDSV_)
				device_->GetCommandList()->ClearDepthStencilView(
					dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		void D3D12RenderContextImpl::RSSetViewport(float topLeftX, float topLeftY, float width, float height)
		{
			device_->BeginFrameIfNeeded();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();

			D3D12_VIEWPORT vp = { topLeftX, topLeftY, width, height, 0.0f, 1.0f };
			list->RSSetViewports(1, &vp);

			// シザーを未指定だと描画されないため、ビューポート全体を既定シザーにする
			D3D12_RECT rect = {
				static_cast<LONG>(topLeftX), static_cast<LONG>(topLeftY),
				static_cast<LONG>(topLeftX + width), static_cast<LONG>(topLeftY + height) };
			list->RSSetScissorRects(1, &rect);
		}

		void D3D12RenderContextImpl::RSSetScissorEnabled(bool) {}

		void D3D12RenderContextImpl::RSSetScissorRect(int x, int y, int w, int h)
		{
			device_->BeginFrameIfNeeded();
			D3D12_RECT rect = { x, y, x + w, y + h };
			device_->GetCommandList()->RSSetScissorRects(1, &rect);
		}

		void D3D12RenderContextImpl::ClearRenderTargetView(uint32_t index, float* clearColor)
		{
			device_->BeginFrameIfNeeded();
			if (index < rtvCount_ && clearColor)
				device_->GetCommandList()->ClearRenderTargetView(rtvHandles_[index], clearColor, 0, nullptr);
		}


		// ── インプットアセンブラ ─────────────────────────────────────────────
		void D3D12RenderContextImpl::IASetVertexBuffer(IVertexBuffer& vb)
		{
			device_->BeginFrameIfNeeded();
			const D3D12_VERTEX_BUFFER_VIEW& view = static_cast<D3D12VertexBuffer&>(vb).GetView();
			device_->GetCommandList()->IASetVertexBuffers(0, 1, &view);
		}

		void D3D12RenderContextImpl::IASetIndexBuffer(IIndexBuffer& ib)
		{
			device_->BeginFrameIfNeeded();
			const D3D12_INDEX_BUFFER_VIEW& view = static_cast<D3D12IndexBuffer&>(ib).GetView();
			device_->GetCommandList()->IASetIndexBuffer(&view);
		}

		void D3D12RenderContextImpl::IASetPrimitiveTopology(PrimitiveTopology topology)
		{
			topology_ = topology;  // PSO トポロジ型 + flush 時の IASetPrimitiveTopology に使う
		}

		void D3D12RenderContextImpl::IASetInputLayout(IShader&)
		{
			// 入力レイアウトは VS リフレクションから PSO 内で構築するため、ここでは何もしない。
		}


		// ── シェーダ / 定数バッファ ──────────────────────────────────────────
		void D3D12RenderContextImpl::VSSetShader(IShader& shader) { pendingVS_ = &shader; }
		void D3D12RenderContextImpl::PSSetShader(IShader& shader) { pendingPS_ = &shader; }

		void D3D12RenderContextImpl::VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			if (startSlot >= D3D12RootSignature::MAX_ROOT_CBV) return;
			device_->BeginFrameIfNeeded();
			EnsureGraphicsRootSig();  // 直前に Dispatch があれば graphics root sig を復元
			device_->GetCommandList()->SetGraphicsRootConstantBufferView(
				startSlot, static_cast<D3D12ConstantBuffer&>(cb).GetGPUAddress());
		}

		void D3D12RenderContextImpl::PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			if (startSlot >= D3D12RootSignature::MAX_ROOT_CBV) return;
			device_->BeginFrameIfNeeded();
			EnsureGraphicsRootSig();
			device_->GetCommandList()->SetGraphicsRootConstantBufferView(
				startSlot, static_cast<D3D12ConstantBuffer&>(cb).GetGPUAddress());
		}

		// テクスチャ/RT/Depth の SRV。staging ヒープ上の SRV を保留テーブルへ記録し、
		// Draw 時に shader-visible ring へコピーしてバインドする。
		// RT/Depth は読み取り前に PIXEL_SHADER_RESOURCE へ遷移する (D3D12SRV::TransitionToSRV)。
		void D3D12RenderContextImpl::PSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv)
		{
			if (startSlot >= SRV_SLOT_COUNT) return;
			// UI の DeferredSRV など IShaderResourceView ラッパに対応するため、GetNativeHandle() 経由で
			// 実体の D3D12SRV* を取得する (D3D11 backend が native SRV ポインタを使うのと同じ多態)。
			auto* s = static_cast<D3D12SRV*>(srv.GetNativeHandle());
			if (!s) { PSUnsetShaderResource(startSlot); return; }
			device_->BeginFrameIfNeeded();
			s->TransitionToSRV(device_->GetCommandList());
			pendingSRV_[startSlot] = s->GetStagingCPUHandle();
			srvDirty_ = true;
		}

		void D3D12RenderContextImpl::PSUnsetShaderResource(uint32_t slot)
		{
			if (slot >= SRV_SLOT_COUNT) return;
			pendingSRV_[slot] = {};  // null SRV で埋める
			srvDirty_ = true;
		}

		// サンプラーはルートシグネチャの静的サンプラー (s0/s1) を使うため no-op。
		void D3D12RenderContextImpl::PSSetSampler(uint32_t, ISamplerState&) {}

		// ── コンピュート (Phase 4: ブルーム) ──
		// 保留 SRV/UAV/CBV を溜め、Dispatch で確定する。
		void D3D12RenderContextImpl::CSSetShader(IShader& shader) { pendingCS_ = &shader; }

		void D3D12RenderContextImpl::CSSetConstantBuffer(uint32_t /*startSlot*/, IConstantBuffer& cb)
		{
			csCBAddr_ = static_cast<D3D12ConstantBuffer&>(cb).GetGPUAddress();
		}

		void D3D12RenderContextImpl::CSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv)
		{
			if (startSlot >= CS_SRV_COUNT) return;
			auto* s = static_cast<D3D12SRV*>(srv.GetNativeHandle());
			if (!s) { csSRV_[startSlot] = {}; return; }
			device_->BeginFrameIfNeeded();
			s->TransitionToComputeSRV(device_->GetCommandList());
			csSRV_[startSlot] = s->GetStagingCPUHandle();
		}

		void D3D12RenderContextImpl::CSUnsetShaderResource(uint32_t slot)
		{
			if (slot < CS_SRV_COUNT) csSRV_[slot] = {};
		}

		void D3D12RenderContextImpl::CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& uav)
		{
			if (startSlot >= CS_UAV_COUNT) return;
			device_->BeginFrameIfNeeded();
			auto& u = static_cast<D3D12UAVHandleRef&>(uav);
			u.TransitionToUAV(device_->GetCommandList());
			csUAV_[startSlot] = u.GetStagingCPUHandle();
		}

		void D3D12RenderContextImpl::CSUnsetUnorderedAccessView(uint32_t slot)
		{
			if (slot < CS_UAV_COUNT) csUAV_[slot] = {};
		}


		// ── 描画 ─────────────────────────────────────────────────────────────
		void D3D12RenderContextImpl::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			FlushDescriptors();
			device_->GetCommandList()->DrawInstanced(vertexCount, 1, startVertexLocation, 0);
		}

		void D3D12RenderContextImpl::DrawIndexed(uint32_t indexCount)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			FlushDescriptors();
			device_->GetCommandList()->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
		}

		void D3D12RenderContextImpl::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			FlushDescriptors();
			device_->GetCommandList()->DrawIndexedInstanced(indexCount, 1, startIndexLocation, 0, 0);
		}

		void D3D12RenderContextImpl::Dispatch(uint32_t x, uint32_t y, uint32_t z)
		{
			device_->BeginFrameIfNeeded();
			auto* cs = static_cast<D3D12Shader*>(pendingCS_);
			if (!cs) return;

			ID3D12Device*              dev  = D3D12GraphicsDeviceImpl::GetStaticDevice();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();
			ID3D12DescriptorHeap*      heap = device_->GetSRVShaderHeap();
			ID3D12RootSignature*       crs  = device_->GetComputeRootSignature();
			if (!dev || !list || !heap || !crs) return;

			// コンピュートルートシグネチャ + PSO
			list->SetComputeRootSignature(crs);
			ID3D12PipelineState* pso = device_->GetPipelineCache()->GetOrCreateCompute(dev, crs, cs);
			if (pso) list->SetPipelineState(pso);

			// b0: ルート CBV
			if (csCBAddr_) list->SetComputeRootConstantBufferView(D3D12RootSignature::CS_PARAM_CBV, csCBAddr_);

			const uint32_t size = device_->GetSRVDescriptorSize();
			D3D12_CPU_DESCRIPTOR_HANDLE nullSrc = device_->GetSRVStagingHeap()->GetCPUDescriptorHandleForHeapStart();
			nullSrc.ptr += static_cast<SIZE_T>(device_->GetNullSRVIndex()) * size;
			D3D12_CPU_DESCRIPTOR_HANDLE nullUav = device_->GetSRVStagingHeap()->GetCPUDescriptorHandleForHeapStart();
			nullUav.ptr += static_cast<SIZE_T>(device_->GetNullUAVIndex()) * size;

			// t0..t1: SRV テーブル (未バインドは null SRV)
			{
				uint32_t base = 0;
				if (device_->AllocateSRVTableRange(CS_SRV_COUNT, base))
				{
					D3D12_CPU_DESCRIPTOR_HANDLE dstCPU = heap->GetCPUDescriptorHandleForHeapStart();
					dstCPU.ptr += static_cast<SIZE_T>(base) * size;
					D3D12_GPU_DESCRIPTOR_HANDLE dstGPU = heap->GetGPUDescriptorHandleForHeapStart();
					dstGPU.ptr += static_cast<UINT64>(base) * size;
					for (uint32_t i = 0; i < CS_SRV_COUNT; ++i)
					{
						D3D12_CPU_DESCRIPTOR_HANDLE slot = dstCPU;
						slot.ptr += static_cast<SIZE_T>(i) * size;
						const D3D12_CPU_DESCRIPTOR_HANDLE src = csSRV_[i].ptr ? csSRV_[i] : nullSrc;
						dev->CopyDescriptorsSimple(1, slot, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					}
					list->SetComputeRootDescriptorTable(D3D12RootSignature::CS_PARAM_SRV_TABLE, dstGPU);
				}
			}

			// u0..u1: UAV テーブル (未バインドスロットは null UAV = 先頭スロットを流用しない)
			if (csUAV_[0].ptr || csUAV_[1].ptr)
			{
				uint32_t base = 0;
				if (device_->AllocateSRVTableRange(CS_UAV_COUNT, base))
				{
					D3D12_CPU_DESCRIPTOR_HANDLE dstCPU = heap->GetCPUDescriptorHandleForHeapStart();
					dstCPU.ptr += static_cast<SIZE_T>(base) * size;
					D3D12_GPU_DESCRIPTOR_HANDLE dstGPU = heap->GetGPUDescriptorHandleForHeapStart();
					dstGPU.ptr += static_cast<UINT64>(base) * size;
					for (uint32_t i = 0; i < CS_UAV_COUNT; ++i)
					{
						D3D12_CPU_DESCRIPTOR_HANDLE slot = dstCPU;
						slot.ptr += static_cast<SIZE_T>(i) * size;
						// 未バインドは null UAV で埋める (UAV テーブルなので SRV null は型不一致)。
						const D3D12_CPU_DESCRIPTOR_HANDLE src = csUAV_[i].ptr ? csUAV_[i] : nullUav;
						dev->CopyDescriptorsSimple(1, slot, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					}
					list->SetComputeRootDescriptorTable(D3D12RootSignature::CS_PARAM_UAV_TABLE, dstGPU);
				}
			}

			list->Dispatch(x, y, z);

			// 次の描画はグラフィクスルートシグネチャ + SRV テーブルを張り直す必要がある。
			srvDirty_          = true;
			graphicsRootDirty_ = true;
		}


		void D3D12RenderContextImpl::IASetIndexBufferGpu(IGpuBuffer& indexBuffer)
		{
			device_->BeginFrameIfNeeded();
			auto& b = static_cast<D3D12GpuBuffer&>(indexBuffer);
			// compute(UAV) → INDEX_BUFFER へ遷移 (バリアが compute 書き込み完了の同期も担う)
			b.Transition(device_->GetCommandList(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
			D3D12_INDEX_BUFFER_VIEW view = b.GetIndexBufferView();
			device_->GetCommandList()->IASetIndexBuffer(&view);
		}


		void D3D12RenderContextImpl::DrawIndexedIndirect(IGpuBuffer& argsBuffer)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			FlushDescriptors();
			auto& b = static_cast<D3D12GpuBuffer&>(argsBuffer);
			b.Transition(device_->GetCommandList(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			ID3D12CommandSignature* sig = device_->GetDrawIndexedCommandSignature();
			if (!sig) return;
			device_->GetCommandList()->ExecuteIndirect(sig, 1, b.GetResource(), 0, nullptr, 0);
		}


		void D3D12RenderContextImpl::UavBarrier(IGpuBuffer& buffer)
		{
			device_->BeginFrameIfNeeded();
			auto& b = static_cast<D3D12GpuBuffer&>(buffer);
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = b.GetResource();
			device_->GetCommandList()->ResourceBarrier(1, &barrier);
		}


		// ── シャドウパス: 深度専用ターゲット ─────────────────────────────────
		void D3D12RenderContextImpl::OMSetDepthOnlyTarget(IDepthMap& depthMap)
		{
			OMSetDepthOnlyTargetSlice(depthMap, 0);
		}

		void D3D12RenderContextImpl::OMSetDepthOnlyTargetSlice(IDepthMap& depthMap, uint32_t slice)
		{
			device_->BeginFrameIfNeeded();
			ID3D12GraphicsCommandList* list = device_->GetCommandList();
			auto& dm = static_cast<D3D12DepthMap&>(depthMap);

			// SRV から書き込みへ戻す: PIXEL_SHADER_RESOURCE → DEPTH_WRITE
			if (dm.GetState() != D3D12_RESOURCE_STATE_DEPTH_WRITE)
			{
				D3D12_RESOURCE_BARRIER b = {};
				b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource   = dm.GetResource();
				b.Transition.StateBefore = dm.GetState();
				b.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
				b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				list->ResourceBarrier(1, &b);
				dm.SetState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
			}

			// カラー RT 無し・DSV のみバインド
			rtvCount_      = 0;
			dsvHandle_     = dm.GetDsv(slice);
			dsFormat_      = DXGI_FORMAT_D32_FLOAT;
			hasDSV_        = true;
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHandle_;
			list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
		}

		void D3D12RenderContextImpl::ClearDepthMap(IDepthMap& depthMap)
		{
			ClearDepthMapSlice(depthMap, 0);
		}

		void D3D12RenderContextImpl::ClearDepthMapSlice(IDepthMap& depthMap, uint32_t slice)
		{
			device_->BeginFrameIfNeeded();
			auto& dm = static_cast<D3D12DepthMap&>(depthMap);
			device_->GetCommandList()->ClearDepthStencilView(
				dm.GetDsv(slice), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}


		// ── 定数バッファ更新 (アップロードヒープへ memcpy) ───────────────────
		void D3D12RenderContextImpl::UpdateConstantBuffer(IConstantBuffer& buf, const void* data)
		{
			static_cast<D3D12ConstantBuffer&>(buf).Update(data);
		}
	}
}
