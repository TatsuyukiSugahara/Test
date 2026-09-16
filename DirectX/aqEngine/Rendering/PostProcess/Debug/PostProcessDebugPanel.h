#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"


namespace aq
{
	namespace rendering
	{
		class MotionBlurEffect;
		class BloomEffect;
		class TonemapEffect;


		/**
		 * ポストエフェクト(MotionBlur + Bloom + トーンマップ)のデバッグ UI パネル。
		 * パイプラインに無いエフェクトは nullptr で渡してよい(該当セクションを出さない)。
		 */
		class PostProcessDebugPanel : public IDebugRenderable
		{
		private:
			MotionBlurEffect* motionBlur_;
			BloomEffect*      bloom_;
			TonemapEffect*    tonemap_;
			bool              show_ = false;


		public:
			PostProcessDebugPanel(MotionBlurEffect* motionBlur, BloomEffect* bloom, TonemapEffect* tonemap);


		public:
			void        DebugRenderMenu()          override;
			void        DebugRender()              override;
			void        RenderContent()            override;
			const char* GetDebugLabel() const      override { return "ポストエフェクト"; }
		};
	}
}
#endif
