#pragma once
#include <memory>
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Math/Vector.h"
#include "Graphics/IShader.h"
#include "Graphics/ISamplerState.h"
#include "Resource/Resource.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * スカイボックスレンダラー。
		 *
		 * キューブマップ (Assets/Sky/SkyCube.dds)・Skybox.fx の VS/PS・サンプラを所有し、
		 * SkyCommand を 1 本積むだけの薄いクラス。Renderer が postProcessRenderer_ と
		 * 同じ位置づけで所有する (空はフォワード構成でも要るので DeferredRenderer には載せない)。
		 *
		 * 生成に失敗しても落とさない (DeferredRenderer の decal と同じ作法)。
		 * その場合は空が出ないだけで、起動と描画は継続する。
		 *
		 * キューブマップは ResourceManager の非同期ロードなので、Create() の成功は
		 * 「ロードを開始できた」ことしか意味しない。実際に描けるかは IsReady() で判定する。
		 */
		class SkyRenderer
		{
		public:
			/**
			 * キューブマップのロード要求・シェーダ生成・サンプラ生成を行う。
			 * @return 描画に必要な資源をそろえられたら true (キューブマップの完了は待たない)
			 */
			bool Create();

			/** 今フレーム空を描けるか (キューブマップのロード完了と SRV の有無まで見る) */
			bool IsReady() const;

			/** SkyCommand を 1 本積む。RT は設定しない (呼び出し側でバインド済みの前提)。 */
			void BuildCommandList(RenderFrame& frame, RenderCommandList& outList) const;

			/** 色調 (rgb) と強度 (a)。既定は無加工の (1,1,1,1)。 */
			inline void SetTint(const math::Vector4& tint) { tint_ = tint; }
			inline const math::Vector4& GetTint() const { return tint_; }

		private:
			/** キューブマップの幅/高さ/isCubemap を 1 回だけログへ出す */
			void LogCubemapOnce() const;

			/** 描画資源 */
			res::RefGPUResource                      cubeMap_;
			std::unique_ptr<graphics::IShader>       skyVS_;
			std::unique_ptr<graphics::IShader>       skyPS_;
			std::unique_ptr<graphics::ISamplerState> sampler_;

			/** 見た目パラメータ */
			math::Vector4 tint_ = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			/** ロード結果のログを 1 回だけ出すためのフラグ (const 経路から更新する) */
			mutable bool logged_ = false;
		};
	}
}
