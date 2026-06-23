#include "aq.h"
#include "UIRenderPipeline.h"
#include "UIRenderItem.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RenderContext.h"
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"

namespace aq
{
	namespace ui
	{
		namespace
		{
			struct CircleGaugeCBData
			{
				float fillAmount;
				float startAngle;
				float clockwise;
				float pad;
			};
		}


		UIRenderPipeline::UIRenderPipeline()
		{
			auto& gfx = graphics::GraphicsDevice::Get();

			// --- シェーダー (同期コンパイル) ---
			vs_            = gfx.CreateShader("Assets/Shader/UISprite.fx",      "VSMain", graphics::IShader::ShaderType::VS);
			standardPS_    = gfx.CreateShader("Assets/Shader/UISprite.fx",      "PSMain", graphics::IShader::ShaderType::PS);
			circleGaugePS_ = gfx.CreateShader("Assets/Shader/UICircleGauge.fx", "PSMain", graphics::IShader::ShaderType::PS);

			if (!vs_ || !standardPS_ || !circleGaugePS_)
			{
				EngineAssertMsg(false, "UI シェーダーコンパイル失敗");
				return;
			}

			// --- 動的 VB (抽象 API 経由) ---
			vb_ = gfx.CreateDynamicVertexBuffer(kMaxVertices, sizeof(UIVertex), nullptr);
			if (!vb_)
			{
				EngineAssertMsg(false, "UI 動的 VB 作成失敗");
				return;
			}

			// --- 動的 IB (R16_UINT, D3D11 直接) ---
			{
				ibCapacityBytes_ = kMaxIndices * sizeof(uint16_t);
				D3D11_BUFFER_DESC desc  = {};
				desc.ByteWidth          = ibCapacityBytes_;
				desc.Usage              = D3D11_USAGE_DYNAMIC;
				desc.BindFlags          = D3D11_BIND_INDEX_BUFFER;
				desc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
				ID3D11Buffer* ibBuf     = nullptr;
				HRESULT hr = graphics::D3D11GraphicsDeviceImpl::GetStaticDevice()
				                         ->CreateBuffer(&desc, nullptr, &ibBuf);
				if (FAILED(hr))
				{
					EngineAssertMsg(false, "UI 動的 IB 作成失敗");
					return;
				}
				ib_ = ibBuf;
			}

			// --- サンプラー (bilinear / clamp) ---
			{
				graphics::SamplerDesc sd;  // デフォルト: MinMagMipLinear, Clamp
				sampler_ = gfx.CreateSamplerState(sd);
				if (!sampler_)
				{
					EngineAssertMsg(false, "UI サンプラー作成失敗");
					return;
				}
			}

			// --- CircleGauge 定数バッファ ---
			{
				CircleGaugeCBData init = { 1.f, 0.f, 1.f, 0.f };
				gaugeCB_ = gfx.CreateConstantBuffer(&init, sizeof(init));
				if (!gaugeCB_)
				{
					EngineAssertMsg(false, "UI gauge CB 作成失敗");
					return;
				}
			}

			ready_ = true;
		}


		UIRenderPipeline::~UIRenderPipeline()
		{
			if (ib_)
			{
				static_cast<ID3D11Buffer*>(ib_)->Release();
				ib_ = nullptr;
			}
		}


		void UIRenderPipeline::Upload(const std::vector<UIVertex>& vtx,
		                               const std::vector<uint16_t>& idx)
		{
			if (!ready_ || vtx.empty() || idx.empty()) return;

			// VB: IVertexBuffer::Update() (DynamicVertexBuffer が Map/Unmap する)
			const uint32_t vbBytes = static_cast<uint32_t>(vtx.size() * sizeof(UIVertex));
			vb_->Update(vtx.data(), vbBytes);

			// IB: D3D11 Map/Unmap (R16_UINT)
			const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint16_t));
			if (ibBytes <= ibCapacityBytes_)
			{
				ID3D11DeviceContext* dxCtx = graphics::D3D11GraphicsDeviceImpl::GetStaticDeviceContext();
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (SUCCEEDED(dxCtx->Map(static_cast<ID3D11Buffer*>(ib_),
				                         0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
				{
					memcpy(mapped.pData, idx.data(), ibBytes);
					dxCtx->Unmap(static_cast<ID3D11Buffer*>(ib_), 0);
				}
			}
		}


		void UIRenderPipeline::BindCommon(graphics::RenderContext& ctx)
		{
			if (!ready_) return;

			ctx.VSSetShader(*vs_);
			ctx.IASetInputLayout(*vs_);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.IASetVertexBuffer(*vb_);

			// IB: R16_UINT, D3D11 直接 (抽象インターフェースは R32_UINT 固定のため)
			ID3D11DeviceContext* dxCtx = graphics::D3D11GraphicsDeviceImpl::GetStaticDeviceContext();
			dxCtx->IASetIndexBuffer(static_cast<ID3D11Buffer*>(ib_), DXGI_FORMAT_R16_UINT, 0);

			ctx.PsSetSampler(0, *sampler_);
		}


		void UIRenderPipeline::BindPS(graphics::RenderContext& ctx, UIShaderType type)
		{
			if (!ready_) return;
			switch (type)
			{
				case UIShaderType::Standard:    ctx.PSSetShader(*standardPS_);    break;
				case UIShaderType::CircleGauge: ctx.PSSetShader(*circleGaugePS_); break;
			}
		}


		void UIRenderPipeline::UpdateCircleGaugeCB(graphics::RenderContext& ctx,
		                                            float fillAmount,
		                                            float startAngle,
		                                            float clockwise)
		{
			if (!ready_ || !gaugeCB_) return;
			CircleGaugeCBData data = { fillAmount, startAngle, clockwise, 0.f };
			ctx.UpdateSubresource(*gaugeCB_, data);
			ctx.PSSetConstantBuffer(0, *gaugeCB_);
		}


		void UIRenderPipeline::DrawIndexed(uint32_t indexCount, uint32_t startIndex)
		{
			if (!ready_) return;
			ID3D11DeviceContext* dxCtx = graphics::D3D11GraphicsDeviceImpl::GetStaticDeviceContext();
			dxCtx->DrawIndexed(indexCount, startIndex, 0);
		}

	} // namespace ui
} // namespace aq
