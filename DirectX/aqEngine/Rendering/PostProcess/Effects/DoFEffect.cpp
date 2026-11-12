#include "aq.h"
#include "DoFEffect.h"
#include "Rendering/PostProcess/Commands/DoFPassCommand.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			static constexpr const char* DOF_SHADER_PATH = "aqEngine/Assets/Shader/DoF.fx";
		}


		bool DoFEffect::Initialize(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// 任意機能。シェーダー / RT が用意できないときは DoF 無効のまま先へ進む
			// (パイプライン全体を止めない。IsEnabled() が false になり入力を素通しする)。
			shader_ = gd.CreateShader(DOF_SHADER_PATH, "main", graphics::IShader::ShaderType::CS);
			if (!shader_)
			{
				aq::StartupMarkf("[dof] shader load FAILED (%s) -> dof disabled", DOF_SHADER_PATH);
				return true;
			}

			outputRTHandle_ = CreateOutputRT(width, height);
			if (!outputRTHandle_.IsValid())
			{
				aq::StartupMark("[dof] output RT create FAILED -> dof disabled");
				return true;
			}

			cocRTHandle_ = CreateCoCRT(width, height);
			if (!cocRTHandle_.IsValid())
			{
				aq::StartupMark("[dof] CoC RT create FAILED -> dof disabled");
				return true;
			}

			DoFCBData init{};
			constantBuffer_ = gd.CreateConstantBuffer(&init, sizeof(init));
			if (!constantBuffer_)
			{
				aq::StartupMark("[dof] constant buffer create FAILED -> dof disabled");
				return true;
			}

			aq::StartupMark("[dof] created (CS ok)");
			return true;
		}


		RenderTargetHandle DoFEffect::CreateOutputRT(const uint32_t width, const uint32_t height)
		{
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R16G16B16A16_Float;  // HDR (Bloom 入力になる)
			desc.hasDepth    = false;
			return graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(desc);
		}


		RenderTargetHandle DoFEffect::CreateCoCRT(const uint32_t width, const uint32_t height)
		{
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R32_Float;  // 符号付きぼけ半径 (画素)
			desc.hasDepth    = false;
			return graphics::GraphicsDevice::Get().CreateOffscreenRenderTarget(desc);
		}


		bool DoFEffect::PrepareResize(const uint32_t width, const uint32_t height)
		{
			AbortResize();

			// Initialize でシェーダーか RT が作れなかった(DoF 無効の)ときは RT を持たないので何もしない。
			if (!shader_ || !outputRTHandle_.IsValid() || !cocRTHandle_.IsValid()) { return true; }

			pendingOutputRTHandle_ = CreateOutputRT(width, height);
			pendingCoCRTHandle_    = CreateCoCRT(width, height);
			if (!pendingOutputRTHandle_.IsValid() || !pendingCoCRTHandle_.IsValid())
			{
				// 片方だけ作れたときも、入れ替えの途中で寸法が食い違わないよう両方片付ける。
				EngineAssertMsg(false, "DoF RT のリサイズ用生成に失敗しました");
				AbortResize();
				return false;
			}
			return true;
		}


		void DoFEffect::CommitResize()
		{
			if (!pendingOutputRTHandle_.IsValid()) { return; }   // 無効(シェーダー / RT 無し)なら何もしない

			auto& gd = graphics::GraphicsDevice::Get();
			if (outputRTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(outputRTHandle_); }
			outputRTHandle_        = pendingOutputRTHandle_;
			pendingOutputRTHandle_ = RenderTargetHandle();

			if (pendingCoCRTHandle_.IsValid())
			{
				if (cocRTHandle_.IsValid()) { gd.ReleaseOffscreenRenderTarget(cocRTHandle_); }
				cocRTHandle_        = pendingCoCRTHandle_;
				pendingCoCRTHandle_ = RenderTargetHandle();
			}
		}


		void DoFEffect::AbortResize()
		{
			if (pendingOutputRTHandle_.IsValid())
			{
				graphics::GraphicsDevice::Get().ReleaseOffscreenRenderTarget(pendingOutputRTHandle_);
			}
			pendingOutputRTHandle_ = RenderTargetHandle();

			if (pendingCoCRTHandle_.IsValid())
			{
				graphics::GraphicsDevice::Get().ReleaseOffscreenRenderTarget(pendingCoCRTHandle_);
			}
			pendingCoCRTHandle_ = RenderTargetHandle();
		}


		bool DoFEffect::IsEnabled(const PostProcessContext& context) const
		{
			// 当該フレームのカメラを受け取っていない(分割画面)ときは無効。
			return enabled_
			    && context.hasCamera
			    && shader_
			    && constantBuffer_
			    && outputRTHandle_.IsValid()
			    && cocRTHandle_.IsValid()
			    && context.worldPosRT.IsValid();
		}


		RenderTargetHandle DoFEffect::Build(
			RenderCommandList&        outList,
			const PostProcessContext& context,
			const RenderTargetHandle  input)
		{
			DoFCBData cb{};
			cb.view           = view_;
			cb.screenWidth    = static_cast<float>(context.width);
			cb.screenHeight   = static_cast<float>(context.height);
			cb.focusDistance  = focusDistance_;
			cb.focusRange     = focusRange_;
			cb.nearTransition = nearTransition_;
			cb.farTransition  = farTransition_;
			cb.maxRadius      = maxRadius_;
			cb.debugView      = debugView_ ? 1u : 0u;

			outList.Enqueue<DoFPassCommand>(
				shader_.get(), constantBuffer_.get(),
				input, context.worldPosRT, outputRTHandle_, cocRTHandle_, cb);

			return outputRTHandle_;
		}


		void DoFEffect::SetFrameCamera(const CameraData& camera)
		{
			view_ = camera.viewMatrix;
		}
	}
}
