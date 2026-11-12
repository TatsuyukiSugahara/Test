#include "aq.h"
#include "DoFEffect.h"
#include "Rendering/PostProcess/Commands/DoFPassCommand.h"
#include "Rendering/PostProcess/Commands/DoFFocusCommand.h"
#include "Graphics/GraphicsTypes.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include <imgui/imgui.h>
#endif


namespace aq
{
	namespace rendering
	{
		namespace
		{
			static constexpr const char* DOF_SHADER_PATH       = "aqEngine/Assets/Shader/DoF.fx";
			static constexpr const char* DOF_FOCUS_SHADER_PATH = "aqEngine/Assets/Shader/DoFFocus.fx";
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

			// オートフォーカスは DoF 本体が揃ったときだけ用意する。失敗してもオートフォーカスだけ使えなくなる。
			InitializeFocus();
			return true;
		}


		void DoFEffect::InitializeFocus()
		{
			auto& gd = graphics::GraphicsDevice::Get();

			focusShader_ = gd.CreateShader(DOF_FOCUS_SHADER_PATH, "main", graphics::IShader::ShaderType::CS);
			if (!focusShader_)
			{
				aq::StartupMarkf("[dof] focus shader load FAILED (%s) -> autofocus disabled", DOF_FOCUS_SHADER_PATH);
				return;
			}

			DoFFocusCBData init{};
			focusConstantBuffer_ = gd.CreateConstantBuffer(&init, sizeof(init));
			if (!focusConstantBuffer_)
			{
				aq::StartupMark("[dof] focus constant buffer create FAILED -> autofocus disabled");
				focusShader_.reset();
				return;
			}

			// 1×1 のピント位置。画面の寸法に依らないので、リサイズでは作り直さない。
			graphics::RenderTargetDesc desc;
			desc.width       = 1;
			desc.height      = 1;
			desc.colorFormat = graphics::PixelFormat::R32_Float;  // ピント位置 (ビュー空間の z)
			desc.hasDepth    = false;
			focusRTHandle_ = gd.CreateOffscreenRenderTarget(desc);
			if (!focusRTHandle_.IsValid())
			{
				aq::StartupMark("[dof] focus RT create FAILED -> autofocus disabled");
				focusConstantBuffer_.reset();
				focusShader_.reset();
				return;
			}

			// 中身は未定義なので、最初の更新では前の値を使わずに書き直す。
			focusResetPending_ = true;
			aq::StartupMark("[dof] focus created (CS ok)");
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

			// フォーカス RT(1×1)は作り直さないが、画面が変わったので次の更新では前の値から寄せずに書き直す。
			focusResetPending_ = true;

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
			cb.useAutoFocus   = 0u;

			// オートフォーカス: DoF より前にフォーカス RT を更新し、DoF はピント位置をそこから読む。
			// 更新(UAV)と読み取り(SRV)は別コマンドの別 dispatch にする(設計書 §0.4 の 5)。
			// 間の UAV → SRV の同期は、他の連続する compute(HiZ のダウンサンプルの連鎖や MotionBlur → DoF)と同じく
			// バインド時の各バックエンドの遷移に任せる(D3D12 は SRV のバインドで遷移バリア、Vulkan は dispatch 前のレイアウト遷移)。
			RenderTargetHandle focusRT;
			if (autoFocusEnabled_ && IsAutoFocusAvailable())
			{
				DoFFocusCBData focusCB{};
				focusCB.view          = view_;
				focusCB.screenWidth   = cb.screenWidth;
				focusCB.screenHeight  = cb.screenHeight;
				focusCB.focusDistance = focusDistance_;
				focusCB.speed         = autoFocusSpeed_;
				focusCB.reset         = focusResetPending_ ? 1u : 0u;

				outList.Enqueue<DoFFocusCommand>(
					focusShader_.get(), focusConstantBuffer_.get(),
					context.worldPosRT, focusRTHandle_, focusCB);
				focusResetPending_ = false;

				cb.useAutoFocus = 1u;
				focusRT         = focusRTHandle_;
			}

			outList.Enqueue<DoFPassCommand>(
				shader_.get(), constantBuffer_.get(),
				input, context.worldPosRT, outputRTHandle_, cocRTHandle_, focusRT, cb);

			return outputRTHandle_;
		}


		void DoFEffect::SetFrameCamera(const CameraData& camera)
		{
			view_ = camera.viewMatrix;
		}

#ifdef AQ_DEBUG_IMGUI
		namespace
		{
			/** DoF のパラメータを触るデバッグパネル (RenderingDebugPanel のタブ "DoF") */
			class DoFDebugPanel final : public IDebugRenderable
			{
			private:
				DoFEffect& dof_;


			public:
				explicit DoFDebugPanel(DoFEffect& dof) : dof_(dof) {}

				void        DebugRenderMenu()     override {}
				void        DebugRender()         override {}
				const char* GetDebugLabel() const override { return "DoF"; }

				void RenderContent() override
				{
					const bool autoAvailable = dof_.IsAutoFocusAvailable();

					bool enabled = dof_.GetEnabled();
					if (ImGui::Checkbox("DoF Enabled", &enabled)) {
						dof_.SetEnabled(enabled);
					}
					ImGui::Text("Auto Focus: %s", autoAvailable ? "Available" : "Unavailable");

					bool debugView = dof_.GetDebugView();
					if (ImGui::Checkbox("Show CoC", &debugView)) {
						dof_.SetDebugView(debugView);
					}
					ImGui::Separator();

					// オートフォーカス中の手動のピント位置は、リセット時の初期値にだけ使われる
					const bool autoFocusing = autoAvailable && dof_.GetAutoFocusEnabled();
					if (autoFocusing) { ImGui::BeginDisabled(); }
					float focusDistance = dof_.GetFocusDistance();
					if (ImGui::SliderFloat("Focus Distance", &focusDistance, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
						dof_.SetFocusDistance(focusDistance);
					}
					if (autoFocusing) { ImGui::EndDisabled(); }
					if (autoFocusing) {
						ImGui::SameLine();
						ImGui::TextUnformatted("(auto)");
					}

					float focusRange = dof_.GetFocusRange();
					if (ImGui::SliderFloat("Focus Range", &focusRange, 0.0f, 20.0f, "%.2f")) {
						dof_.SetFocusRange(focusRange);
					}

					float nearTransition = dof_.GetNearTransition();
					if (ImGui::SliderFloat("Near Transition", &nearTransition, 0.0f, 20.0f, "%.2f")) {
						dof_.SetNearTransition(nearTransition);
					}

					float farTransition = dof_.GetFarTransition();
					if (ImGui::SliderFloat("Far Transition", &farTransition, 0.0f, 20.0f, "%.2f")) {
						dof_.SetFarTransition(farTransition);
					}

					float maxRadius = dof_.GetMaxRadius();
					if (ImGui::SliderFloat("Max Radius", &maxRadius, 0.0f, 32.0f, "%.1f")) {
						dof_.SetMaxRadius(maxRadius);
					}
					ImGui::Separator();

					// オートフォーカスの資源が無いときは触れなくする
					if (!autoAvailable) { ImGui::BeginDisabled(); }
					bool autoFocus = dof_.GetAutoFocusEnabled();
					if (ImGui::Checkbox("Auto Focus", &autoFocus)) {
						dof_.SetAutoFocusEnabled(autoFocus);
					}
					float autoFocusSpeed = dof_.GetAutoFocusSpeed();
					if (ImGui::SliderFloat("Auto Focus Speed", &autoFocusSpeed, 0.01f, 1.0f, "%.2f")) {
						dof_.SetAutoFocusSpeed(autoFocusSpeed);
					}
					if (!autoAvailable) { ImGui::EndDisabled(); }
				}
			};
		}


		std::unique_ptr<IDebugRenderable> DoFEffect::CreateDebugPanel()
		{
			return std::make_unique<DoFDebugPanel>(*this);
		}
#endif
	}
}
