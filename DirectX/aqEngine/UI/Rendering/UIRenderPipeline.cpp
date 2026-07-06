#include "aq.h"
#include "UIRenderPipeline.h"
#include "UIRenderItem.h"
#include "UIBatchRenderCommand.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RenderContext.h"

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

			// UISDFText.fx の SdfTextCB と一致させること (b1, 64 bytes)
			struct SdfTextCBData
			{
				math::Vector4 outlineColor;     // 16
				math::Vector4 shadowColor;      // 16
				math::Vector2 shadowOffsetUV;   // 8
				float         shadowSoftness;   // 4
				float         outlineWidth;     // 4
				float         smoothing;        // 4
				float         pad0, pad1, pad2; // 12
			};
		}


		UIRenderPipeline::UIRenderPipeline()
		{
			auto& gfx = graphics::GraphicsDevice::Get();

			// --- シェーダー (同期コンパイル) ---
			vs_            = gfx.CreateShader("Assets/Shader/UISprite.fx",      "VSMain", graphics::IShader::ShaderType::VS);
			standardPS_    = gfx.CreateShader("Assets/Shader/UISprite.fx",      "PSMain", graphics::IShader::ShaderType::PS);
			circleGaugePS_ = gfx.CreateShader("Assets/Shader/UICircleGauge.fx", "PSMain", graphics::IShader::ShaderType::PS);
			sdfTextPS_     = gfx.CreateShader("Assets/Shader/UISDFText.fx",     "PSMain", graphics::IShader::ShaderType::PS);

			if (!vs_ || !standardPS_ || !circleGaugePS_ || !sdfTextPS_)
			{
				EngineAssertMsg(false, "UI シェーダーコンパイル失敗");
				return;
			}

			// --- 動的 VB (抽象 API 経由) ---
			vb_ = gfx.CreateDynamicVertexBuffer(MAX_VERTICES, sizeof(UIVertex), nullptr);
			if (!vb_)
			{
				EngineAssertMsg(false, "UI 動的 VB 作成失敗");
				return;
			}

			// --- 動的 IB (R16_UINT, 抽象 API 経由) ---
			ib_ = gfx.CreateDynamicIndexBuffer(MAX_INDICES, graphics::IndexFormat::UInt16, nullptr);
			if (!ib_)
			{
				EngineAssertMsg(false, "UI 動的 IB 作成失敗");
				return;
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

			// --- SdfText 定数バッファ (b1) ---
			{
				SdfTextCBData init = {};
				init.smoothing = 0.08f;
				sdfTextCB_ = gfx.CreateConstantBuffer(&init, sizeof(init));
				if (!sdfTextCB_)
				{
					EngineAssertMsg(false, "UI sdfText CB 作成失敗");
					return;
				}
			}

			ready_ = true;
		}


		UIRenderPipeline::~UIRenderPipeline() = default;


		void UIRenderPipeline::Upload(const std::vector<UIVertex>& vtx,
		                               const std::vector<uint16_t>& idx)
		{
			if (!ready_ || vtx.empty() || idx.empty()) return;
			if (vtx.size() > MAX_VERTICES || idx.size() > MAX_INDICES) return;

			// VB: IVertexBuffer::Update() (DynamicVertexBuffer が Map/Unmap する)
			const uint32_t vbBytes = static_cast<uint32_t>(vtx.size() * sizeof(UIVertex));
			vb_->Update(vtx.data(), vbBytes);

			// IB: IIndexBuffer::Update() (R16_UINT 動的バッファ)
			const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint16_t));
			ib_->Update(idx.data(), ibBytes);
		}


		void UIRenderPipeline::BindCommon(graphics::RenderContext& ctx)
		{
			if (!ready_) return;

			ctx.VSSetShader(*vs_);
			ctx.IASetInputLayout(*vs_);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.IASetVertexBuffer(*vb_);
			ctx.IASetIndexBuffer(*ib_);  // フォーマット (R16_UINT) はバッファが保持
			ctx.PsSetSampler(0, *sampler_);
		}


		void UIRenderPipeline::BindPS(graphics::RenderContext& ctx, UIShaderType type)
		{
			if (!ready_) return;
			switch (type)
			{
				case UIShaderType::Standard:    ctx.PSSetShader(*standardPS_);    break;
				case UIShaderType::CircleGauge: ctx.PSSetShader(*circleGaugePS_); break;
				case UIShaderType::SdfText:     ctx.PSSetShader(*sdfTextPS_);     break;
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


		void UIRenderPipeline::UpdateSdfTextCB(graphics::RenderContext& ctx,
		                                         const SdfTextParams& params)
		{
			if (!ready_ || !sdfTextCB_) return;
			SdfTextCBData data = {};
			data.outlineColor   = params.outlineColor;
			data.shadowColor    = params.shadowColor;
			data.shadowOffsetUV = params.shadowOffsetUV;
			data.shadowSoftness = params.shadowSoftness;
			data.outlineWidth   = params.outlineWidth;
			data.smoothing      = params.smoothing;
			ctx.UpdateSubresource(*sdfTextCB_, data);
			ctx.PSSetConstantBuffer(1, *sdfTextCB_);
		}

	} // namespace ui
} // namespace aq
