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
		 * G-Buffer の worldPos からカメラまでの距離を作り、隣り合う画素との相対差が大きい場所を
		 * エッジとして線色で塗る。直前の色(Output。無ければ Scene)を読んで自前の RT へ書き、
		 * それを新しい Output にする(compute は入力と出力を同じ RT にできないため)。
		 *
		 * **`PipelinePresets::Standard()` には入らない任意パス**。使うゲームが自分で挿す:
		 *   auto builder = BuildStandardPipeline(preset);
		 *   auto outline = std::make_unique<OutlinePass>();
		 *   outline->SetColor(math::Vector3(0.0f, 0.05f, 0.1f));
		 *   builder.InsertBefore<UIPass>(std::move(outline));
		 *
		 * worldPos を書くパス(GBufferPass)が前に無い列(フォワードのみ構成)では、何もせず
		 * 素通しする(Setup 失敗はパイプライン全体の失敗になるため、黙って無効化する)。
		 */
		class OutlinePass final : public IRenderPass
		{
		private:
			/** シェーダ定数(Outline.fx の b0 と同じ並び) */
			struct OutlineCBData
			{
				math::Vector3 cameraPos = {};
				float         threshold = 0.02f;
				math::Vector3 color     = math::Vector3(0.0f, 0.0f, 0.0f);
				float         intensity = 0.8f;
				uint32_t      width     = 0;
				uint32_t      height    = 0;
				int32_t       thickness = 1;
				int32_t       padding   = 0;
			};

			std::unique_ptr<graphics::IShader>         shader_;
			std::unique_ptr<graphics::IConstantBuffer> constantBuffer_;

			/** 自前の出力 RT。これが新しい Output になる */
			RenderTargetHandle outputRT_;
			/** Setup 時点の Output(無ければ INVALID。その場合は毎フレームの Scene を読む) */
			RenderTargetHandle inputRT_;
			/** GBuffer2(worldPos)。掲示板に無ければ INVALID = このパスは何もしない */
			RenderTargetHandle worldPosRT_;

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
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** 線の見た目。ゲームが好きな値を入れる(既定は控えめな黒) */
		public:
			inline void SetColor(const math::Vector3& color)   { params_.color     = color; }
			inline void SetIntensity(const float intensity)    { params_.intensity = intensity; }
			/** エッジとみなす相対深度差(0.01 = 1%)。小さいほど線が増える */
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
