#pragma once
#include "ECS/ECS.h"
#include "Ocean/OceanMesh.h"
#include "Ocean/OceanData.h"
#ifdef AQ_DEBUG_IMGUI
#include <string>
#include <imgui/imgui.h>
#endif

namespace aq
{
	namespace ecs
	{
		// ============================================================
		// OceanComponent — 海サーフェス ECS コンポーネント
		//
		// 使い方:
		//   auto entity = EntityContext::Get().CreateEntity<TransformComponent, OceanComponent>();
		//   auto* tc = entity.GetComponent<TransformComponent>();
		//   tc->transform.localPosition.Set(-50.f, -1.f, -50.f);
		//
		//   auto* ocean = entity.GetComponent<OceanComponent>();
		//   ocean::OceanParams params;
		//   params.size = 200.0f;
		//   ocean->Initialize(params);
		// ============================================================
		class OceanComponent : public IComponent
		{
			ecsComponent(aq::ecs::OceanComponent);

		public:
			void Initialize(const ocean::OceanParams& params);

			bool IsCompleted() const { return state_ == State::Completed; }

			ocean::OceanMesh*        GetMesh()   { return &mesh_; }
			const ocean::OceanParams& GetParams() const { return params_; }
			ocean::OceanParams&       GetParams()       { return params_; }

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				// Mesh/texture params: reinit on mouse-release
				bool reinit = false;
				{
					float s = params_.size;
					ImGui::DragFloat("Size", &s, 1.0f, 1.0f, 10000.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) { params_.size = s; reinit = true; }
				}
				{
					int r = static_cast<int>(params_.resolution);
					ImGui::DragInt("Resolution", &r, 1.0f, 4, 1024);
					if (ImGui::IsItemDeactivatedAfterEdit()) { params_.resolution = static_cast<uint32_t>(r); reinit = true; }
				}
				reinit |= visitor.FieldPath("Normal Map 1", normalMapPath1_);
				reinit |= visitor.FieldPath("Normal Map 2", normalMapPath2_);
				if (reinit && state_ == State::Completed)
				{
					params_.normalMapPath1 = normalMapPath1_.empty() ? nullptr : normalMapPath1_.c_str();
					params_.normalMapPath2 = normalMapPath2_.empty() ? nullptr : normalMapPath2_.c_str();
					Initialize(params_);
				}
				// Shader params: live edit
				visitor.Field("Deep Color",    params_.deepColor);
				visitor.Field("Shallow Color", params_.shallowColor);
				visitor.Field("Sky Color",     params_.skyColor);
				visitor.Field("Fresnel Bias",  params_.fresnelBias);
				visitor.Field("Fresnel Scale", params_.fresnelScale);
				visitor.Field("Fresnel Power", params_.fresnelPower);
				visitor.Field("Sun Shininess", params_.sunShininess);
				visitor.Field("Sun Intensity", params_.sunIntensity);
				visitor.Field("Wave Q",        params_.waveQ);
				for (int i = 0; i < 4; ++i)
				{
					char label[16];
					sprintf_s(label, sizeof(label), "Wave %d", i);
					ImGui::PushID(i);
					if (ImGui::TreeNode(label))
					{
						visitor.Field("Dir X",      params_.waves[i].dirX);
						visitor.Field("Dir Z",      params_.waves[i].dirZ);
						visitor.Field("Amplitude",  params_.waves[i].amplitude);
						visitor.Field("Wavelength", params_.waves[i].wavelength);
						visitor.Field("Speed",      params_.waves[i].speed);
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				visitor.ReadOnly("state", IsCompleted() ? "Completed" : "Invalid");
			}
#endif

		private:
			enum class State : uint8_t { Invalid, Completed };
			State              state_  = State::Invalid;
			ocean::OceanParams params_;
			ocean::OceanMesh   mesh_;
#ifdef AQ_DEBUG_IMGUI
			std::string normalMapPath1_;
			std::string normalMapPath2_;
#endif
		};
	}
}
