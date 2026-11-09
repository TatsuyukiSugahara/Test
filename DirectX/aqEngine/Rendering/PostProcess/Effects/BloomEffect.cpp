#include "aq.h"
#include "BloomEffect.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		bool BloomEffect::Initialize(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// シェーダーロード
			extractShader_ = gd.CreateShader(
				"aqEngine/Assets/Shader/BloomBrightExtract.fx", "main", graphics::IShader::ShaderType::CS);
			dualBlurDownShader_ = gd.CreateShader(
				"aqEngine/Assets/Shader/DualBlurDown.fx", "main", graphics::IShader::ShaderType::CS);
			dualBlurUpShader_ = gd.CreateShader(
				"aqEngine/Assets/Shader/DualBlurUp.fx", "main", graphics::IShader::ShaderType::CS);
			dualBlurUpAccumShader_ = gd.CreateShader(
				"aqEngine/Assets/Shader/DualBlurUpAccum.fx", "main", graphics::IShader::ShaderType::CS);

			if (!extractShader_ || !dualBlurDownShader_ || !dualBlurUpShader_ || !dualBlurUpAccumShader_)
			{
				EngineAssertMsg(false, "Bloom シェーダーのロードに失敗しました");
				return false;
			}

			// リニアクランプサンプラー
			graphics::SamplerDesc samplerDesc;
			samplerDesc.filter   = graphics::FilterMode::MinMagMipLinear;
			samplerDesc.addressU = graphics::AddressMode::Clamp;
			samplerDesc.addressV = graphics::AddressMode::Clamp;
			sampler_ = gd.CreateSamplerState(samplerDesc);
			if (!sampler_)
			{
				EngineAssertMsg(false, "Bloom サンプラーの生成に失敗しました");
				return false;
			}

			// 輝度抽出 RT とピラミッド RT
			if (!CreateTargets(width, height, brightRTHandle_, pyramidRTHandles_))
			{
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


		bool BloomEffect::CreateTargets(const uint32_t width, const uint32_t height,
		                                RenderTargetHandle& outBright, RenderTargetHandle* outPyramid)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// 輝度抽出 RT は HDR (R16F)。シーン RT が HDR になったため、1.0 超の輝度をクランプせず
			// ブルームピラミッドへ運ぶ。
			graphics::RenderTargetDesc brightDesc;
			brightDesc.width       = width;
			brightDesc.height      = height;
			brightDesc.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;
			brightDesc.hasDepth    = false;

			outBright = gd.CreateOffscreenRenderTarget(brightDesc);
			if (!outBright.IsValid())
			{
				EngineAssertMsg(false, "Bloom RT の生成に失敗しました");
				return false;
			}

			// ピラミッド RT：pyramid[i] のサイズ = (width >> (i+1)) × (height >> (i+1))
			for (uint32_t i = 0; i < MAX_LEVELS; ++i)
			{
				graphics::RenderTargetDesc desc;
				desc.width       = (width  >> (i + 1)) > 0u ? (width  >> (i + 1)) : 1u;
				desc.height      = (height >> (i + 1)) > 0u ? (height >> (i + 1)) : 1u;
				desc.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;  // HDR ブルームピラミッド
				desc.hasDepth    = false;
				outPyramid[i] = gd.CreateOffscreenRenderTarget(desc);
				if (!outPyramid[i].IsValid())
				{
					EngineAssertMsg(false, "Bloom ピラミッド RT の生成に失敗しました");
					return false;
				}
			}

			return true;
		}


		void BloomEffect::ReleaseTargets(RenderTargetHandle& bright, RenderTargetHandle* pyramid)
		{
			auto& gd = graphics::GraphicsDevice::Get();
			if (bright.IsValid()) { gd.ReleaseOffscreenRenderTarget(bright); }
			bright = RenderTargetHandle();
			for (uint32_t i = 0; i < MAX_LEVELS; ++i)
			{
				if (pyramid[i].IsValid()) { gd.ReleaseOffscreenRenderTarget(pyramid[i]); }
				pyramid[i] = RenderTargetHandle();
			}
		}


		bool BloomEffect::PrepareResize(const uint32_t width, const uint32_t height)
		{
			AbortResize();
			if (!CreateTargets(width, height, pendingBrightRTHandle_, pendingPyramidRTHandles_))
			{
				AbortResize();   // 途中まで作れた一時 RT を解放する
				return false;
			}
			return true;
		}


		void BloomEffect::CommitResize()
		{
			ReleaseTargets(brightRTHandle_, pyramidRTHandles_);
			brightRTHandle_ = pendingBrightRTHandle_;
			for (uint32_t i = 0; i < MAX_LEVELS; ++i) { pyramidRTHandles_[i] = pendingPyramidRTHandles_[i]; }
			pendingBrightRTHandle_ = RenderTargetHandle();
			for (uint32_t i = 0; i < MAX_LEVELS; ++i) { pendingPyramidRTHandles_[i] = RenderTargetHandle(); }
		}


		void BloomEffect::AbortResize()
		{
			ReleaseTargets(pendingBrightRTHandle_, pendingPyramidRTHandles_);
		}


		bool BloomEffect::IsEnabled(const PostProcessContext&) const
		{
			return extractShader_
			    && dualBlurDownShader_
			    && dualBlurUpShader_
			    && dualBlurUpAccumShader_
			    && sampler_
			    && bloomCB_
			    && brightRTHandle_.IsValid();
		}


		RenderTargetHandle BloomEffect::Build(
			RenderCommandList&        outList,
			const PostProcessContext& context,
			const RenderTargetHandle  input)
		{
			outList.Enqueue<BloomPassCommand>(
				extractShader_.get(),
				dualBlurDownShader_.get(),
				dualBlurUpShader_.get(),
				dualBlurUpAccumShader_.get(),
				sampler_.get(),
				bloomCB_.get(),
				input,
				brightRTHandle_,
				pyramidRTHandles_,
				threshold_,
				intensity_,
				blurPasses_,
				context.width,
				context.height);

			return brightRTHandle_;
		}
	}
}
