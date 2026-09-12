#include "aq.h"
#include "OffscreenScenePass.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Rendering/DrawItemCommand.h"
#include "Rendering/InstancedDrawItemCommand.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		OffscreenScenePass::OffscreenScenePass()
		{
		}


		OffscreenScenePass::~OffscreenScenePass()
		{
		}


		bool OffscreenScenePass::Create(const uint32_t width, const uint32_t height)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			// カラーのみ。深度は DeferredRenderer の GBuffer0 が同じ寸法で持つ。
			graphics::RenderTargetDesc desc;
			desc.width       = width;
			desc.height      = height;
			desc.colorFormat = graphics::PixelFormat::R8G8B8A8_Unorm;
			desc.hasDepth    = false;
			sceneRTHandle_   = gd.CreateOffscreenRenderTarget(desc);
			if (!sceneRTHandle_.IsValid()) { return false; }

			// このパス専用の縮小 GBuffer。メイン解像度の GBuffer とは別物なので、
			// RTV と DSV の寸法が食い違わない。
			auto deferred = std::make_unique<DeferredRenderer>();
			if (!deferred->Create(width, height)) { return false; }

			deferredRenderer_ = std::move(deferred);
			width_            = width;
			height_           = height;
			return true;
		}


		void OffscreenScenePass::SetClearColor(const math::Vector4& color)
		{
			clearColor_[0] = color.x;
			clearColor_[1] = color.y;
			clearColor_[2] = color.z;
			clearColor_[3] = color.w;
		}


		void OffscreenScenePass::BuildCommandList(const RenderFrame& frame, RenderCommandList& outList) const
		{
			if (!IsReady()) { return; }

			// 背景をクリアし、ビューポートをこのパスの寸法へ合わせる。
			// ビューポートはコンテキストに残るが、メインパスは自前で設定し直すので影響しない。
			outList.Enqueue<SetRenderTargetCommand>(sceneRTHandle_);
			outList.Enqueue<ClearRenderTargetCommand>(0u, clearColor_);
			outList.Enqueue<SetViewportCommand>(
				0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));

			// G-Buffer パス (カラー x4 + 深度のクリアもここで行われる)。
			deferredRenderer_->BuildGBufferCommandList(frame, outList);

			// ディファードライティングパス (シーン RT へ書き込む)。
			deferredRenderer_->BuildLightingCommandList(frame, outList, sceneRTHandle_);

			// フォワードパス。GBuffer0 の深度でテストしながら描く。
			outList.Enqueue<SetRenderTargetWithDepthCommand>(
				sceneRTHandle_, deferredRenderer_->GetGBuffer0Handle());
			// 半透明アイテム(ゴースト等)は描かない。このパスはミニマップ等の俯瞰表示用で
			// ブレンド設定を切り替えないため、そのまま描くと不透明として出てしまう。
			for (const RenderItem& item : frame.forwardItems) {
				if (item.translucent) { continue; }
				outList.Enqueue<DrawItemCommand>(item, frame.camera);
			}
			for (const InstancedRenderItem& item : frame.instancedItems) {
				outList.Enqueue<InstancedDrawItemCommand>(item, frame.camera);
			}
		}


		ShadowCBData OffscreenScenePass::MakeNeutralShadowCBData()
		{
			// 単位行列のままだと原点付近のワールド座標がシャドウマップ範囲内と判定されてしまうため、
			// 全ての座標を ndc.z = -1 (範囲外) へ落とす行列を入れて影を確実に無効化する。
			const math::Matrix4x4 outside(
				0.0f, 0.0f,  0.0f, 0.0f,
				0.0f, 0.0f,  0.0f, 0.0f,
				0.0f, 0.0f,  0.0f, 0.0f,
				0.0f, 0.0f, -1.0f, 1.0f);

			ShadowCBData shadow;
			for (uint32_t i = 0; i < MaxShadowCascades; ++i) {
				shadow.lightViewProj[i] = outside;
			}
			shadow.cascadeCount = 0;
			return shadow;
		}
	}
}
