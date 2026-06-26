#include "aq.h"
#include "D3D12Common.h"
#include "D3D12RenderContextImpl.h"
#include "D3D12GraphicsDeviceImpl.h"
#include "D3D12Buffers.h"
#include "D3D12Shader.h"
#include "D3D12PipelineStateCache.h"
#include "D3D12RootSignature.h"

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
		}


		D3D12RenderContextImpl::D3D12RenderContextImpl(D3D12GraphicsDeviceImpl* device)
			: device_(device)
		{
		}


		void D3D12RenderContextImpl::FlushPipeline()
		{
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
			key.rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;   // P1b: バックバッファ固定
			key.dsFormat = DXGI_FORMAT_UNKNOWN;          // P3 で深度対応

			ID3D12PipelineState* pso = cache->GetOrCreate(
				D3D12GraphicsDeviceImpl::GetStaticDevice(), device_->GetRootSignature(), vs, ps, key);
			if (pso) list->SetPipelineState(pso);
			list->IASetPrimitiveTopology(ToD3DTopology(topology_));
		}


		// ── 出力マージャ / ラスタライザ ──────────────────────────────────────
		void D3D12RenderContextImpl::OMSetRenderTargets(uint32_t, IRenderTarget*)
		{
			// P1b: RTV はフレーム先頭でバックバッファに束ねる。オフスクリーン RT は P3。
			device_->BeginFrameIfNeeded();
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

		void D3D12RenderContextImpl::ClearRenderTargetView(uint32_t, float*)
		{
			// P1b: クリアはフレーム先頭 (BeginFrameIfNeeded) で実施済み。
			device_->BeginFrameIfNeeded();
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
			device_->GetCommandList()->SetGraphicsRootConstantBufferView(
				startSlot, static_cast<D3D12ConstantBuffer&>(cb).GetGPUAddress());
		}

		void D3D12RenderContextImpl::PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			if (startSlot >= D3D12RootSignature::MAX_ROOT_CBV) return;
			device_->BeginFrameIfNeeded();
			device_->GetCommandList()->SetGraphicsRootConstantBufferView(
				startSlot, static_cast<D3D12ConstantBuffer&>(cb).GetGPUAddress());
		}

		// P2: テクスチャ SRV テーブル / サンプラーは未実装
		void D3D12RenderContextImpl::PSSetShaderResource(uint32_t, IShaderResourceView&) {}
		void D3D12RenderContextImpl::PSUnsetShaderResource(uint32_t) {}
		void D3D12RenderContextImpl::PSSetSampler(uint32_t, ISamplerState&) {}

		// P4: コンピュート系は未実装
		void D3D12RenderContextImpl::CSSetShader(IShader&) {}
		void D3D12RenderContextImpl::CSSetConstantBuffer(uint32_t, IConstantBuffer&) {}
		void D3D12RenderContextImpl::CSSetShaderResource(uint32_t, IShaderResourceView&) {}
		void D3D12RenderContextImpl::CSUnsetShaderResource(uint32_t) {}
		void D3D12RenderContextImpl::CSSetUnorderedAccessView(uint32_t, IUnorderedAccessView&) {}
		void D3D12RenderContextImpl::CSUnsetUnorderedAccessView(uint32_t) {}


		// ── 描画 ─────────────────────────────────────────────────────────────
		void D3D12RenderContextImpl::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			device_->GetCommandList()->DrawInstanced(vertexCount, 1, startVertexLocation, 0);
		}

		void D3D12RenderContextImpl::DrawIndexed(uint32_t indexCount)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			device_->GetCommandList()->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
		}

		void D3D12RenderContextImpl::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation)
		{
			device_->BeginFrameIfNeeded();
			FlushPipeline();
			device_->GetCommandList()->DrawIndexedInstanced(indexCount, 1, startIndexLocation, 0, 0);
		}

		void D3D12RenderContextImpl::Dispatch(uint32_t, uint32_t, uint32_t) {}


		// ── 定数バッファ更新 (アップロードヒープへ memcpy) ───────────────────
		void D3D12RenderContextImpl::UpdateConstantBuffer(IConstantBuffer& buf, const void* data)
		{
			static_cast<D3D12ConstantBuffer&>(buf).Update(data);
		}
	}
}
