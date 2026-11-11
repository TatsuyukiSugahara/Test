#include "aq.h"
#include "SSRRenderer.h"
#include "SSRCompositeCommand.h"
#include "Graphics/GraphicsTypes.h"
#include "Rendering/IBL/IBLEnvironment.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			static constexpr const char* SSR_COMPOSITE_SHADER_PATH = "aqEngine/Assets/Shader/SSRComposite.fx";
		}


		bool SSRRenderer::Create()
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// SSRComposite.fx の VS/PS (同期生成)
			compositeVS_ = gd.CreateShader(SSR_COMPOSITE_SHADER_PATH, "VSMain", graphics::IShader::ShaderType::VS);
			compositePS_ = gd.CreateShader(SSR_COMPOSITE_SHADER_PATH, "PSMain", graphics::IShader::ShaderType::PS);
			if (!compositeVS_ || !compositePS_) {
				aq::StartupMarkf("[ssr] shader load FAILED (%s) -> ssr disabled", SSR_COMPOSITE_SHADER_PATH);
				compositeVS_.reset();
				compositePS_.reset();
				return false;
			}

			aq::StartupMark("[ssr] created (composite VS/PS ok)");
			return true;
		}


		void SSRRenderer::BuildCommandList(const DeferredRenderer& deferred,
		                                   const std::shared_ptr<const IBLEnvironment>& ibl,
		                                   RenderCommandList& outList) const
		{
			if (!enabled_ || !compositeVS_ || !compositePS_ || !ibl) { return; }

			// P1: トレースが無いので、ssr (当たった色 rgb + 確信度 a) を testMode の固定値で与える。
			// P2 でトレースの出力 RT に置き換える。
			const math::Vector4 ssrTestValue = (testMode_ == TEST_MODE_FULL_BLACK)
				? math::Vector4(0.0f, 0.0f, 0.0f, 1.0f)
				: math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);

			// 強度は値で写す (デバッグ UI からの変更が実行待ちのコマンドと競合しない。§0.2)
			outList.Enqueue<SSRCompositeCommand>(
				*compositeVS_, *compositePS_,
				deferred.GetGBuffer0Handle(), deferred.GetGBuffer1Handle(),
				deferred.GetGBuffer2Handle(), deferred.GetGBuffer3Handle(),
				ibl, ibl->GetIntensity(), ssrTestValue);
		}
	}
}
