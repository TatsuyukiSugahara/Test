#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "D3D11RenderContextImpl.h"
#include "D3D11GraphicsDeviceImpl.h"
#include "D3D11RenderResources.h"
#include "D3D11Buffers.h"
#include "D3D11Shader.h"
#include "D3D11DepthMap.h"


namespace aq
{
	namespace graphics
	{
		D3D11RenderContextImpl::D3D11RenderContextImpl(ID3D11DeviceContext* context)
			: context_(context)
			, viewport_{}
			, renderTargetViews_{}
			, depthStencilView_(nullptr)
			, renderTargetViewNum_(0)
		{
			ID3D11Device* dev = D3D11GraphicsDeviceImpl::GetStaticDevice();

			// DepthStencilState
			D3D11_DEPTH_STENCIL_DESC dssDesc = {};
			dssDesc.DepthFunc = D3D11_COMPARISON_LESS;

			dssDesc.DepthEnable    = TRUE;
			dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dev->CreateDepthStencilState(&dssDesc, &dssReadWrite_);

			dssDesc.DepthEnable    = TRUE;
			dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			dev->CreateDepthStencilState(&dssDesc, &dssReadOnly_);

			dssDesc.DepthEnable    = FALSE;
			dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			dev->CreateDepthStencilState(&dssDesc, &dssDisabled_);

			// BlendState
			D3D11_BLEND_DESC blendDesc = {};
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			// Opaque
			blendDesc.RenderTarget[0].BlendEnable = FALSE;
			dev->CreateBlendState(&blendDesc, &bsOpaque_);

			// AlphaBlend: src.a * src + (1 - src.a) * dst
			blendDesc.RenderTarget[0].BlendEnable    = TRUE;
			blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
			dev->CreateBlendState(&blendDesc, &bsAlphaBlend_);

			// Additive: src.a * src + dst
			blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
			dev->CreateBlendState(&blendDesc, &bsAdditive_);

			// Premultiplied: src (already premultiplied) + (1 - src.a) * dst
			blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			dev->CreateBlendState(&blendDesc, &bsPremultiplied_);

			// DecalColor: RGB は AlphaBlend、アルファ ch は書き込まない
			// (GBuffer0.a = metallic を保護する。デカール投影パス専用)
			blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
			dev->CreateBlendState(&blendDesc, &bsDecalColor_);
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			// RasterizerState (ScissorEnable あり / なし)
			D3D11_RASTERIZER_DESC rsDesc = {};
			rsDesc.FillMode              = D3D11_FILL_SOLID;
			rsDesc.CullMode              = D3D11_CULL_NONE;
			rsDesc.DepthClipEnable       = TRUE;
			rsDesc.ScissorEnable         = FALSE;
			dev->CreateRasterizerState(&rsDesc, &rsDefault_);

			rsDesc.ScissorEnable = TRUE;
			dev->CreateRasterizerState(&rsDesc, &rsScissor_);
		}


		D3D11RenderContextImpl::~D3D11RenderContextImpl()
		{
			if (dssReadWrite_)    { dssReadWrite_->Release();    dssReadWrite_    = nullptr; }
			if (dssReadOnly_)     { dssReadOnly_->Release();     dssReadOnly_     = nullptr; }
			if (dssDisabled_)     { dssDisabled_->Release();     dssDisabled_     = nullptr; }
			if (bsOpaque_)        { bsOpaque_->Release();        bsOpaque_        = nullptr; }
			if (bsAlphaBlend_)    { bsAlphaBlend_->Release();    bsAlphaBlend_    = nullptr; }
			if (bsAdditive_)      { bsAdditive_->Release();      bsAdditive_      = nullptr; }
			if (bsPremultiplied_) { bsPremultiplied_->Release(); bsPremultiplied_ = nullptr; }
			if (bsDecalColor_)    { bsDecalColor_->Release();    bsDecalColor_    = nullptr; }
			if (rsDefault_)       { rsDefault_->Release();       rsDefault_        = nullptr; }
			if (rsScissor_)       { rsScissor_->Release();       rsScissor_        = nullptr; }
		}


		void D3D11RenderContextImpl::OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget)
		{
			memory::Clear(renderTargetViews_, sizeof(renderTargetViews_));
			depthStencilView_ = nullptr;
			if (renderTarget) {
				// IRenderTarget* は常に RenderTarget[] の先頭を指す (D3D11 backend 内保証)
				RenderTarget* rts = static_cast<RenderTarget*>(renderTarget);
				depthStencilView_ = rts[0].GetDepthStencilView();
				for (uint32_t i = 0; i < numViews; ++i) {
					renderTargetViews_[i] = rts[i].GetrenderTargetView();
				}
			}
			context_->OMSetRenderTargets(numViews, renderTargetViews_, depthStencilView_);
			renderTargetViewNum_ = numViews;
		}


		void D3D11RenderContextImpl::OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets)
		{
			memory::Clear(renderTargetViews_, sizeof(renderTargetViews_));
			depthStencilView_ = nullptr;
			for (uint32_t i = 0; i < numViews; ++i) {
				auto* rt = static_cast<RenderTarget*>(renderTargets[i]);
				renderTargetViews_[i] = rt ? rt->GetrenderTargetView() : nullptr;
			}
			if (renderTargets[0]) {
				depthStencilView_ = static_cast<RenderTarget*>(renderTargets[0])->GetDepthStencilView();
			}
			context_->OMSetRenderTargets(numViews, renderTargetViews_, depthStencilView_);
			renderTargetViewNum_ = numViews;
		}


		void D3D11RenderContextImpl::OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
		{
			memory::Clear(renderTargetViews_, sizeof(renderTargetViews_));
			renderTargetViews_[0] = static_cast<RenderTarget&>(colorRT).GetrenderTargetView();
			depthStencilView_     = static_cast<RenderTarget&>(depthSourceRT).GetDepthStencilView();
			context_->OMSetRenderTargets(1, renderTargetViews_, depthStencilView_);
			renderTargetViewNum_ = 1;
		}


		void D3D11RenderContextImpl::OMSetDepthMode(DepthMode mode)
		{
			ID3D11DepthStencilState* dss = nullptr;
			switch (mode) {
				case DepthMode::ReadWrite: dss = dssReadWrite_; break;
				case DepthMode::ReadOnly:  dss = dssReadOnly_;  break;
				case DepthMode::Disabled:  dss = dssDisabled_;  break;
			}
			context_->OMSetDepthStencilState(dss, 0);
		}


		void D3D11RenderContextImpl::OMSetBlendMode(BlendMode mode)
		{
			ID3D11BlendState* bs = nullptr;
			switch (mode) {
				case BlendMode::Opaque:        bs = bsOpaque_;        break;
				case BlendMode::AlphaBlend:    bs = bsAlphaBlend_;    break;
				case BlendMode::Additive:      bs = bsAdditive_;      break;
				case BlendMode::Premultiplied: bs = bsPremultiplied_; break;
				case BlendMode::DecalColor:    bs = bsDecalColor_;    break;
			}
			const float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
			context_->OMSetBlendState(bs, blendFactor, 0xFFFFFFFF);
		}


		void D3D11RenderContextImpl::RSSetScissorEnabled(bool enabled)
		{
			context_->RSSetState(enabled ? rsScissor_ : rsDefault_);
		}


		void D3D11RenderContextImpl::RSSetScissorRect(int x, int y, int w, int h)
		{
			D3D11_RECT rect = { x, y, x + w, y + h };
			context_->RSSetScissorRects(1, &rect);
		}


		void D3D11RenderContextImpl::RSSetViewport(float topLeftX, float topLeftY, float width, float height)
		{
			viewport_.Width    = width;
			viewport_.Height   = height;
			viewport_.TopLeftX = topLeftX;
			viewport_.TopLeftY = topLeftY;
			viewport_.MinDepth = 0.0f;
			viewport_.MaxDepth = 1.0f;
			context_->RSSetViewports(1, &viewport_);
		}


		void D3D11RenderContextImpl::ClearRenderTargetView(uint32_t index, float* clearColor)
		{
			if (renderTargetViews_ && index < renderTargetViewNum_) {
				context_->ClearRenderTargetView(renderTargetViews_[index], clearColor);
			}
		}


		void D3D11RenderContextImpl::ClearDepthBuffer()
		{
			if (depthStencilView_) {
				context_->ClearDepthStencilView(depthStencilView_, D3D11_CLEAR_DEPTH, 1.0f, 0);
			}
		}


		void D3D11RenderContextImpl::IASetVertexBuffer(IVertexBuffer& vb)
		{
			uint32_t offset = 0;
			uint32_t stride = vb.GetStride();
			auto* d3dBuf    = static_cast<VertexBuffer&>(vb).GetBody();
			context_->IASetVertexBuffers(0, 1, &d3dBuf, &stride, &offset);
		}


		void D3D11RenderContextImpl::IASetIndexBuffer(IIndexBuffer& ib)
		{
			auto& d3dIB     = static_cast<IndexBuffer&>(ib);
			DXGI_FORMAT fmt = (d3dIB.GetFormat() == IndexFormat::UInt16)
			                      ? DXGI_FORMAT_R16_UINT
			                      : DXGI_FORMAT_R32_UINT;
			context_->IASetIndexBuffer(d3dIB.GetBody(), fmt, 0);
		}


		void D3D11RenderContextImpl::IASetPrimitiveTopology(PrimitiveTopology topology)
		{
			context_->IASetPrimitiveTopology(ToD3D11(topology));
		}


		void D3D11RenderContextImpl::IASetInputLayout(IShader& vsShader)
		{
			auto* layout = static_cast<Shader&>(vsShader).GetInputLayoutD3D11();
			context_->IASetInputLayout(layout);
		}


		void D3D11RenderContextImpl::VSSetShader(IShader& shader)
		{
			auto* vs = static_cast<Shader&>(shader).GetShaderAs<ID3D11VertexShader>();
			context_->VSSetShader(vs, nullptr, 0);
		}


		void D3D11RenderContextImpl::VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			auto* d3dBuf = static_cast<ConstantBuffer&>(cb).GetBody();
			context_->VSSetConstantBuffers(startSlot, 1, &d3dBuf);
		}


		void D3D11RenderContextImpl::PSSetShader(IShader& shader)
		{
			auto* ps = static_cast<Shader&>(shader).GetShaderAs<ID3D11PixelShader>();
			context_->PSSetShader(ps, nullptr, 0);
		}


		void D3D11RenderContextImpl::PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			auto* d3dBuf = static_cast<ConstantBuffer&>(cb).GetBody();
			context_->PSSetConstantBuffers(startSlot, 1, &d3dBuf);
		}


		void D3D11RenderContextImpl::PSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv)
		{
			auto* d3dSRV = static_cast<ID3D11ShaderResourceView*>(srv.GetNativeHandle());
			context_->PSSetShaderResources(startSlot, 1, &d3dSRV);
		}


		void D3D11RenderContextImpl::PSUnsetShaderResource(uint32_t slot)
		{
			ID3D11ShaderResourceView* view[] = { nullptr };
			context_->PSSetShaderResources(slot, 1, view);
		}


		void D3D11RenderContextImpl::PSSetSampler(uint32_t startSlot, ISamplerState& ss)
		{
			auto* d3dSS = static_cast<SamplerState&>(ss).GetBody();
			context_->PSSetSamplers(startSlot, 1, &d3dSS);
		}


		void D3D11RenderContextImpl::CSSetShader(IShader& shader)
		{
			auto* cs = static_cast<Shader&>(shader).GetShaderAs<ID3D11ComputeShader>();
			context_->CSSetShader(cs, nullptr, 0);
		}


		void D3D11RenderContextImpl::CSUnsetShader()
		{
			context_->CSSetShader(nullptr, nullptr, 0);
		}


		void D3D11RenderContextImpl::CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			auto* d3dBuf = static_cast<ConstantBuffer&>(cb).GetBody();
			context_->CSSetConstantBuffers(startSlot, 1, &d3dBuf);
		}


		void D3D11RenderContextImpl::CSSetSampler(uint32_t startSlot, ISamplerState& ss)
		{
			auto* d3dSS = static_cast<SamplerState&>(ss).GetBody();
			context_->CSSetSamplers(startSlot, 1, &d3dSS);
		}


		void D3D11RenderContextImpl::CSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv)
		{
			auto* d3dSRV = static_cast<ShaderResourceView&>(srv).GetBody();
			context_->CSSetShaderResources(startSlot, 1, &d3dSRV);
		}


		void D3D11RenderContextImpl::CSUnsetShaderResource(uint32_t slot)
		{
			ID3D11ShaderResourceView* view[] = { nullptr };
			context_->CSSetShaderResources(slot, 1, view);
		}


		void D3D11RenderContextImpl::CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& uav)
		{
			auto* d3dUAV = static_cast<UnorderedAccessView&>(uav).GetBody();
			context_->CSSetUnorderedAccessViews(startSlot, 1, &d3dUAV, nullptr);
		}


		void D3D11RenderContextImpl::CSUnsetUnorderedAccessView(uint32_t slot)
		{
			ID3D11UnorderedAccessView* view[] = { nullptr };
			context_->CSSetUnorderedAccessViews(slot, 1, view, nullptr);
		}


		void D3D11RenderContextImpl::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
		{
			context_->Draw(vertexCount, startVertexLocation);
		}


		void D3D11RenderContextImpl::DrawIndexed(uint32_t indexCount)
		{
			context_->DrawIndexed(indexCount, 0, 0);
		}


		void D3D11RenderContextImpl::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation)
		{
			context_->DrawIndexed(indexCount, startIndexLocation, 0);
		}


		void D3D11RenderContextImpl::Dispatch(uint32_t x, uint32_t y, uint32_t z)
		{
			context_->Dispatch(x, y, z);
		}


		void D3D11RenderContextImpl::UpdateConstantBuffer(IConstantBuffer& buf, const void* data)
		{
			auto* d3dBuf = static_cast<ConstantBuffer&>(buf).GetBody();
			if (d3dBuf) {
				context_->UpdateSubresource(d3dBuf, 0, nullptr, data, 0, 0);
			}
		}


		void D3D11RenderContextImpl::OMSetDepthOnlyTarget(IDepthMap& depthMap)
		{
			auto& d3dDM = static_cast<D3D11DepthMap&>(depthMap);
			ID3D11RenderTargetView* nullRTV = nullptr;
			depthStencilView_ = d3dDM.GetDSV();
			context_->OMSetRenderTargets(0, &nullRTV, depthStencilView_);
			renderTargetViewNum_ = 0;
		}


		void D3D11RenderContextImpl::ClearDepthMap(IDepthMap& depthMap)
		{
			auto& d3dDM = static_cast<D3D11DepthMap&>(depthMap);
			context_->ClearDepthStencilView(d3dDM.GetDSV(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		}


		void D3D11RenderContextImpl::OMSetDepthOnlyTargetSlice(IDepthMap& depthMap, uint32_t slice)
		{
			auto& d3dDM = static_cast<D3D11DepthMap&>(depthMap);
			ID3D11RenderTargetView* nullRTV = nullptr;
			depthStencilView_ = d3dDM.GetDSV(slice);
			context_->OMSetRenderTargets(0, &nullRTV, depthStencilView_);
			renderTargetViewNum_ = 0;
		}


		void D3D11RenderContextImpl::ClearDepthMapSlice(IDepthMap& depthMap, uint32_t slice)
		{
			auto& d3dDM = static_cast<D3D11DepthMap&>(depthMap);
			context_->ClearDepthStencilView(d3dDM.GetDSV(slice), D3D11_CLEAR_DEPTH, 1.0f, 0);
		}


		void D3D11RenderContextImpl::PSUnsetShader()
		{
			context_->PSSetShader(nullptr, nullptr, 0);
		}


		void D3D11RenderContextImpl::RSSetState(ID3D11RasterizerState* state)
		{
			context_->RSSetState(state);
		}


		D3D11_PRIMITIVE_TOPOLOGY D3D11RenderContextImpl::ToD3D11(PrimitiveTopology topology)
		{
			switch (topology) {
				case PrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
				case PrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
				case PrimitiveTopology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
				case PrimitiveTopology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
				default:                               return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			}
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D11
