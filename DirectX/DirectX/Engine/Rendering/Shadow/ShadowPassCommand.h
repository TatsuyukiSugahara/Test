#pragma once
#include <memory>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderFrame.h"
#include "Graphics/IDepthMap.h"
#include "Graphics/IRenderTarget.h"


namespace engine
{
	namespace rendering
	{
		/** シャドウパス開始: DSV バインド + ビューポート設定 + デプスクリア */
		class ShadowBeginCommand final : public IRenderCommand
		{
		public:
			ShadowBeginCommand(graphics::IDepthMap& depthMap, uint32_t resolution);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		private:
			graphics::IDepthMap* depthMap_;
			uint32_t             resolution_;
		};


		/** シャドウパス 1 ドローコール: デプスのみ書き込み */
		class ShadowCastCommand final : public IRenderCommand
		{
		public:
			ShadowCastCommand(const RenderItem& item, std::shared_ptr<graphics::IShader> shadowVS);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		private:
			RenderItem                        item_;
			std::shared_ptr<graphics::IShader> shadowVS_;
		};


		/** シャドウパス終了: メイン RT 復元 + シャドウ SRV を t4/s1 にバインド */
		class ShadowEndCommand final : public IRenderCommand
		{
		public:
			ShadowEndCommand(graphics::IDepthMap&     depthMap,
			                 graphics::IRenderTarget* mainRT,
			                 float                    mainW,
			                 float                    mainH);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		private:
			graphics::IDepthMap*     depthMap_;
			graphics::IRenderTarget* mainRT_;
			float                    mainW_;
			float                    mainH_;
		};
	}
}
