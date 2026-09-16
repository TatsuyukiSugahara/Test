#include "aq.h"
#include "PipelineBuilder.h"
#include "RenderPipeline.h"


namespace aq
{
	namespace rendering
	{
		PipelineBuilder& PipelineBuilder::Add(std::unique_ptr<IRenderPass> pass)
		{
			passes_.push_back(std::move(pass));
			return *this;
		}


		std::unique_ptr<RenderPipeline> PipelineBuilder::Build(const uint32_t width, const uint32_t height)
		{
			// 1. IsSupported() が false のパスを列から外す。
			{
				std::vector<std::unique_ptr<IRenderPass>> supported;
				supported.reserve(passes_.size());
				for (auto& pass : passes_) {
					if (pass->IsSupported()) {
						supported.push_back(std::move(pass));
					} else {
						aq::StartupMarkf("[pipeline] 除外: %s (この環境では動かない)", pass->GetName());
					}
				}
				passes_ = std::move(supported);
			}

			// 2. Reads の検証。初期状態は Scene のみが在るものとする。
			//    P1 は検証の宣言だけ入れて、この段では失敗させない(ログのみ)。
			{
				std::vector<PassResourceKey> available;
				available.push_back(PassResourceKeys::Scene);

				for (auto& pass : passes_) {
					PassDeclaration decl;
					pass->DeclareResources(decl);

					for (const PassResourceKey key : decl.GetReads()) {
						bool found = false;
						for (const PassResourceKey a : available) {
							if (a == key) { found = true; break; }
						}
						if (!found) {
							aq::StartupMarkf("[pipeline] %s は未登録のキーを読む", pass->GetName());
						}
					}

					for (const PassResourceKey key : decl.GetWrites()) {
						available.push_back(key);
					}
				}
			}

			// 3. Frame scope が View scope の列の途中に挟まっていないかの検証(P1 はログのみ)。
			{
				size_t firstView = passes_.size();
				size_t lastView  = 0;
				for (size_t i = 0; i < passes_.size(); ++i) {
					if (passes_[i]->GetScope() == PassScope::View) {
						if (firstView == passes_.size()) { firstView = i; }
						lastView = i;
					}
				}
				if (firstView < passes_.size()) {
					for (size_t i = firstView + 1; i < lastView; ++i) {
						if (passes_[i]->GetScope() == PassScope::Frame) {
							aq::StartupMarkf("[pipeline] View scope の列の途中に Frame scope の %s がある", passes_[i]->GetName());
						}
					}
				}
			}

			// 4. PassResources を作り、Setup を順に呼ぶ。
			//    Scene は毎フレーム差し替える(RenderPipeline::Build/BuildViews が上書きする)ので、
			//    ここでは仮の INVALID ハンドルを置くだけでよい。
			PassResources resources;
			resources.Set(PassResourceKeys::Scene, RenderTargetHandle{});

			for (auto& pass : passes_) {
				if (!pass->Setup(resources, width, height)) {
					aq::StartupMarkf("[pipeline] Setup 失敗: %s", pass->GetName());
					return nullptr;
				}
			}

			// 5. 確定した列をログに出す。
			{
				std::string joined;
				for (size_t i = 0; i < passes_.size(); ++i) {
					if (i > 0) { joined += " > "; }
					joined += passes_[i]->GetName();
				}
				aq::StartupMarkf("[pipeline] 確定: %s", joined.c_str());
			}

			return std::make_unique<RenderPipeline>(std::move(passes_), std::move(resources));
		}
	}
}
