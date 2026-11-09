#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"


namespace sample
{
	/**
	 * 画面の縁を暗くするビネット(README の 4 歩目「描画パスを足す」の実例)。
	 *
	 * エンジンのパイプラインに挿す、ゲーム側のレンダーパス。
	 * トーンマップ後の色(掲示板の Output)を読み、自前の RT へ書いて、それを新しい Output にする。
	 * シェーダはエンジン所有の Vignette.fx(compute)。
	 */
	class VignettePass final : public aq::rendering::IRenderPass
	{
	private:
		/** シェーダ定数(Vignette.fx の b0 と同じ並び) */
		struct VignetteCBData
		{
			float    strength = 0.6f;   // 縁の暗さ(0 で無効)
			float    radius   = 0.45f;  // 暗くなり始める中心からの距離(0〜1)
			uint32_t width    = 0;
			uint32_t height   = 0;
		};

		std::unique_ptr<aq::graphics::IShader>         shader_;
		std::unique_ptr<aq::graphics::IConstantBuffer> constantBuffer_;
		aq::rendering::RenderTargetHandle              outputRT_;   // 自前の出力 RT
		aq::rendering::RenderTargetHandle              inputRT_;    // Wire 時点の Output(トーンマップ結果)。無ければ Scene を読む
		VignetteCBData                                 params_;

		/** リサイズ用の一時領域(PrepareResize で作り、CommitResize で本体へ入れ替える) */
		aq::rendering::RenderTargetHandle              pendingOutputRT_;
		uint32_t                                       pendingWidth_  = 0;
		uint32_t                                       pendingHeight_ = 0;


	public:
		VignettePass() = default;
		~VignettePass() override = default;


		/**
		 * IRenderPass
		 */
	public:
		inline const char*             GetName()  const override { return "VignettePass"; }
		inline aq::rendering::PassScope GetScope() const override { return aq::rendering::PassScope::Frame; }
		bool IsSupported() const override;
		void DeclareResources(aq::rendering::PassDeclaration& decl) const override;
		bool Setup(aq::rendering::PassResources& res, const uint32_t width, const uint32_t height) override;
		inline aq::rendering::ResizePolicy GetResizePolicy() const override { return aq::rendering::ResizePolicy::Resizable; }
		bool PrepareResize(const uint32_t width, const uint32_t height) override;
		void CommitResize(aq::rendering::PassResources& res) override;
		void AbortResize() override;
		void Build(aq::rendering::RenderFrame& frame, const aq::rendering::PassViewInfo& view,
		           aq::rendering::PassResources& res, aq::rendering::RenderCommandList& outList) override;


	public:
		inline void  SetStrength(const float strength) { params_.strength = strength; }
		inline float GetStrength() const               { return params_.strength; }


	private:
		/** 出力 RT を作る(Setup / PrepareResize 共用) */
		static aq::rendering::RenderTargetHandle CreateOutputRT(const uint32_t width, const uint32_t height);

		/** 掲示板から入力(前段の Output)を取り直し、自分の RT を Output として登録する(Setup / CommitResize 共用) */
		void Wire(aq::rendering::PassResources& res);
	};
}
