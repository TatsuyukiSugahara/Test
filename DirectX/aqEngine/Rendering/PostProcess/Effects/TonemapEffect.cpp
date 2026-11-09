#include "aq.h"
#include "TonemapEffect.h"
#include "Rendering/PostProcess/Commands/TonemapPassCommand.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		bool TonemapEffect::Initialize(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			compositeShader_ = gd.CreateShader(
				"aqEngine/Assets/Shader/BloomComposite.fx", "main", graphics::IShader::ShaderType::CS);
			if (!compositeShader_)
			{
				EngineAssertMsg(false, "Bloom シェーダーのロードに失敗しました");
				return false;
			}

			// 最終 RT のみ LDR (トーンマップ済みの表示出力)。
			finalRTHandle_ = CreateFinalRT(width, height);
			if (!finalRTHandle_.IsValid())
			{
				EngineAssertMsg(false, "Bloom RT の生成に失敗しました");
				return false;
			}

			// 定数バッファ
			BloomCBData initData{};
			bloomCB_ = gd.CreateConstantBuffer(&initData, sizeof(initData));
			if (!bloomCB_)
			{
				EngineAssertMsg(false, "Bloom CB の生成に失敗しました");
				return false;
			}

			return true;
		}


		RenderTargetHandle TonemapEffect::CreateFinalRT(const uint32_t width, const uint32_t height)
		{
			graphics::RenderTargetDesc finalDesc;
			finalDesc.width       = width;
			finalDesc.height      = height;
			finalDesc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;  // トーンマップ後 LDR 表示
			finalDesc.hasDepth    = false;
			return graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(finalDesc);
		}


		bool TonemapEffect::PrepareResize(const uint32_t width, const uint32_t height)
		{
			AbortResize();
			pendingFinalRTHandle_ = CreateFinalRT(width, height);
			if (!pendingFinalRTHandle_.IsValid())
			{
				EngineAssertMsg(false, "Tonemap RT のリサイズ用生成に失敗しました");
				return false;
			}
			return true;
		}


		void TonemapEffect::CommitResize()
		{
			auto& gd = graphics::GraphicsDevice::Get();
			if (finalRTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(finalRTHandle_); }
			finalRTHandle_        = pendingFinalRTHandle_;
			pendingFinalRTHandle_ = RenderTargetHandle();
		}


		void TonemapEffect::AbortResize()
		{
			if (pendingFinalRTHandle_.IsValid())
			{
				graphics::GraphicsDevice::Get().ReleaseOffscreenRenderTarget(pendingFinalRTHandle_);
			}
			pendingFinalRTHandle_ = RenderTargetHandle();
		}


		bool TonemapEffect::IsEnabled(const PostProcessContext&) const
		{
			// HDR → LDR 変換が無いと表示できないため常時 ON。
			return true;
		}


		RenderTargetHandle TonemapEffect::Build(
			RenderCommandList&        outList,
			const PostProcessContext& context,
			const RenderTargetHandle  input)
		{
			outList.Enqueue<TonemapPassCommand>(
				compositeShader_.get(),
				bloomCB_.get(),
				input,
				bloomRTHandle_,
				finalRTHandle_,
				bloomIntensity_,
				context.width,
				context.height,
				exposure_,
				static_cast<uint32_t>(tonemapMode_),
				whitePoint_,
				applyGamma_ ? 1u : 0u);

			return finalRTHandle_;
		}
	}
}
