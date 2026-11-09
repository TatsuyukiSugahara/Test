#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"
#include "Math/Vector.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 輪郭線パス(設計書/レンダーパイプライン設計.md P5)。
		 *
		 * G-Buffer の法線(GBuffer1)を隣り合う画素と比べ、シルエット(片方だけ背景)と
		 * 折り目(法線が大きく向きを変える)を線色で塗る。直前の色(Output。無ければ Scene)を
		 * 読んで自前の RT へ書き、それを新しい Output にする
		 * (compute は入力と出力を同じ RT にできないため)。
		 *
		 * **深度(worldPos)は使わない。** worldPos は half float なので刻み幅が座標の大きさに
		 * 比例し(|座標| 2360m のコースでは 2m)、距離の差分を見ると平坦な路面や地形に
		 * 刻みの境目が等間隔の線として出てしまう。
		 *
		 * **`PipelinePresets::Standard()` には入らない任意パス**。使うゲームが自分で挿す:
		 *   auto builder = BuildStandardPipeline(preset);
		 *   auto outline = std::make_unique<OutlinePass>();
		 *   outline->SetColor(math::Vector3(0.0f, 0.05f, 0.1f));
		 *   builder.InsertBefore<UIPass>(std::move(outline));
		 *
		 * 法線を書くパス(GBufferPass)が前に無い列(フォワードのみ構成)では、何もせず
		 * 素通しする(Setup 失敗はパイプライン全体の失敗になるため、黙って無効化する)。
		 */
		class OutlinePass final : public IRenderPass
		{
		private:
			/** シェーダ定数(Outline.fx の b0 と同じ並び) */
			struct OutlineCBData
			{
				math::Vector3 color     = math::Vector3(0.0f, 0.0f, 0.0f);
				float         intensity = 0.8f;
				float         threshold = 0.25f;
				uint32_t      width     = 0;
				uint32_t      height    = 0;
				int32_t       thickness = 1;
			};

			std::unique_ptr<graphics::IShader>         shader_;
			std::unique_ptr<graphics::IConstantBuffer> constantBuffer_;

			/** 自前の出力 RT。これが新しい Output になる */
			RenderTargetHandle outputRT_;
			/** Setup 時点の Output(無ければ INVALID。その場合は毎フレームの Scene を読む) */
			RenderTargetHandle inputRT_;
			/** GBuffer1(法線)。Wire が掲示板から取り直す。無効(active_ = false)なら INVALID */
			RenderTargetHandle normalRT_;
			/** PrepareResize が作った新しい出力 RT(CommitResize で outputRT_ へ入れ替わる) */
			RenderTargetHandle pendingOutputRT_;
			/** pendingOutputRT_ の寸法(CommitResize で params_ へ反映する) */
			uint32_t pendingWidth_  = 0;
			uint32_t pendingHeight_ = 0;

			/**
			 * 有効か。Setup が GBuffer1 の有無で 1 回だけ決める(無ければ資源を作らず素通し)。
			 * リサイズ時も掲示板は見直さず、この判定を使う。
			 */
			bool active_ = false;

			OutlineCBData params_;


		public:
			OutlinePass() = default;
			~OutlinePass() override = default;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "OutlinePass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }
			bool PrepareResize(const uint32_t width, const uint32_t height) override;
			void CommitResize(PassResources& res) override;
			void AbortResize() override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


		private:
			/** シェーダ・出力 RT・定数バッファの生成(active_ が true のときだけ呼ぶ) */
			bool CreateTargets(const uint32_t width, const uint32_t height);
			/** 出力 RT のみ生成する(Setup と PrepareResize で共有) */
			static RenderTargetHandle CreateOutputRT(const uint32_t width, const uint32_t height);
			/** 法線と入力を掲示板から取り直し、自分の出力を Output として登録する(Setup と CommitResize で共有) */
			void Wire(PassResources& res);


			/** 線の見た目。ゲームが好きな値を入れる(既定は控えめな黒) */
		public:
			inline void SetColor(const math::Vector3& color)   { params_.color     = color; }
			inline void SetIntensity(const float intensity)    { params_.intensity = intensity; }
			/** 折り目とみなす法線の差 `1 - dot(n0, n1)`(0.25 ≒ 41 度)。小さいほど線が増える */
			inline void SetThreshold(const float threshold)    { params_.threshold = threshold; }
			/** 線の太さ(隣接画素までの距離。px) */
			inline void SetThickness(const int32_t thickness)  { params_.thickness = thickness; }

			inline const math::Vector3& GetColor() const { return params_.color; }
			inline float GetIntensity()            const { return params_.intensity; }
			inline float GetThreshold()            const { return params_.threshold; }
			inline int32_t GetThickness()          const { return params_.thickness; }
		};
	}
}
