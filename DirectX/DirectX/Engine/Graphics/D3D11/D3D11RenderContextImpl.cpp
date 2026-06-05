#include "../../EnginePreCompile.h"
#include "D3D11RenderContextImpl.h"
#include "D3D11RenderResources.h"


namespace engine
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
		}


		void D3D11RenderContextImpl::OMSetRenderTargets(uint32_t numViews, RenderTarget* renderTarget)
		{
			memory::Clear(renderTargetViews_, sizeof(renderTargetViews_));
			depthStencilView_ = nullptr;
			if (renderTarget) {
				depthStencilView_ = renderTarget[0].GetDepthStencilView();
				for (uint32_t i = 0; i < numViews; ++i) {
					renderTargetViews_[i] = renderTarget[i].GetrenderTargetView();
				}
			}
			context_->OMSetRenderTargets(numViews, renderTargetViews_, depthStencilView_);
			renderTargetViewNum_ = numViews;
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
				context_->ClearDepthStencilView(depthStencilView_, D3D11_CLEAR_DEPTH, 1.0f, 0);
			}
		}


		void D3D11RenderContextImpl::IASetVertexBuffer(IVertexBuffer& vb)
		{
			uint32_t offset = 0;
			uint32_t stride = vb.GetStride();
			auto* d3dBuf    = static_cast<ID3D11Buffer*>(vb.GetNativeHandle());
			context_->IASetVertexBuffers(0, 1, &d3dBuf, &stride, &offset);
		}


		void D3D11RenderContextImpl::IASetIndexBuffer(IIndexBuffer& ib)
		{
			auto* d3dBuf = static_cast<ID3D11Buffer*>(ib.GetNativeHandle());
			context_->IASetIndexBuffer(d3dBuf, DXGI_FORMAT_R32_UINT, 0);
		}


		void D3D11RenderContextImpl::IASetPrimitiveTopology(PrimitiveTopology topology)
		{
			context_->IASetPrimitiveTopology(ToD3D11(topology));
		}


		void D3D11RenderContextImpl::IASetInputLayout(IShader& vsShader)
		{
			auto* layout = static_cast<ID3D11InputLayout*>(vsShader.GetInputLayout());
			context_->IASetInputLayout(layout);
		}


		void D3D11RenderContextImpl::VSSetShader(IShader& shader)
		{
			auto* vs = static_cast<ID3D11VertexShader*>(shader.GetNativeHandle());
			context_->VSSetShader(vs, nullptr, 0);
		}


		void D3D11RenderContextImpl::VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			auto* d3dBuf = static_cast<ID3D11Buffer*>(cb.GetNativeHandle());
			context_->VSSetConstantBuffers(startSlot, 1, &d3dBuf);
		}


		void D3D11RenderContextImpl::PSSetShader(IShader& shader)
		{
			auto* ps = static_cast<ID3D11PixelShader*>(shader.GetNativeHandle());
			context_->PSSetShader(ps, nullptr, 0);
		}


		void D3D11RenderContextImpl::PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			auto* d3dBuf = static_cast<ID3D11Buffer*>(cb.GetNativeHandle());
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
			auto* d3dSS = static_cast<ID3D11SamplerState*>(ss.GetNativeHandle());
			context_->PSSetSamplers(startSlot, 1, &d3dSS);
		}


		void D3D11RenderContextImpl::CSSetShader(IShader& shader)
		{
			auto* cs = static_cast<ID3D11ComputeShader*>(shader.GetNativeHandle());
			context_->CSSetShader(cs, nullptr, 0);
		}


		void D3D11RenderContextImpl::CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb)
		{
			auto* d3dBuf = static_cast<ID3D11Buffer*>(cb.GetNativeHandle());
			context_->CSSetConstantBuffers(startSlot, 1, &d3dBuf);
		}


		void D3D11RenderContextImpl::CSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv)
		{
			auto* d3dSRV = static_cast<ID3D11ShaderResourceView*>(srv.GetNativeHandle());
			context_->CSSetShaderResources(startSlot, 1, &d3dSRV);
		}


		void D3D11RenderContextImpl::CSUnsetShaderResource(uint32_t slot)
		{
			ID3D11ShaderResourceView* view[] = { nullptr };
			context_->CSSetShaderResources(slot, 1, view);
		}


		void D3D11RenderContextImpl::CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& uav)
		{
			auto* d3dUAV = static_cast<ID3D11UnorderedAccessView*>(uav.GetNativeHandle());
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


		void D3D11RenderContextImpl::Dispatch(uint32_t x, uint32_t y, uint32_t z)
		{
			context_->Dispatch(x, y, z);
		}


		void D3D11RenderContextImpl::UpdateSubresourceRaw(void* gpuBuffer, const void* srcData)
		{
			if (gpuBuffer) {
				context_->UpdateSubresource(static_cast<ID3D11Resource*>(gpuBuffer), 0, nullptr, srcData, 0, 0);
			}
		}


		void D3D11RenderContextImpl::CopyResourceRaw(void* dest, void* src)
		{
			if (dest && src) {
				context_->CopyResource(
					static_cast<ID3D11Resource*>(dest),
					static_cast<ID3D11Resource*>(src)
				);
			}
		}


		void D3D11RenderContextImpl::MapRaw(void* buffer, uint32_t subResource, MapType mapType, uint32_t flags, MappedSubresource& mapped)
		{
			if (buffer) {
				D3D11_MAPPED_SUBRESOURCE d3dMapped = {};
				context_->Map(static_cast<ID3D11Resource*>(buffer), subResource, ToD3D11(mapType), flags, &d3dMapped);
				mapped.pData      = d3dMapped.pData;
				mapped.rowPitch   = d3dMapped.RowPitch;
				mapped.depthPitch = d3dMapped.DepthPitch;
			}
		}


		void D3D11RenderContextImpl::UnmapRaw(void* buffer, uint32_t subResource)
		{
			if (buffer) {
				context_->Unmap(static_cast<ID3D11Resource*>(buffer), subResource);
			}
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


		D3D11_MAP D3D11RenderContextImpl::ToD3D11(MapType mapType)
		{
			switch (mapType) {
				case MapType::Read:             return D3D11_MAP_READ;
				case MapType::Write:            return D3D11_MAP_WRITE;
				case MapType::ReadWrite:        return D3D11_MAP_READ_WRITE;
				case MapType::WriteDiscard:     return D3D11_MAP_WRITE_DISCARD;
				case MapType::WriteNoOverwrite: return D3D11_MAP_WRITE_NO_OVERWRITE;
				default:                        return D3D11_MAP_WRITE_DISCARD;
			}
		}
	}
}
