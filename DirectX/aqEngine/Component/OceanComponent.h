#pragma once
#include "ECS/ECS.h"
#include "Ocean/OceanMesh.h"
#include "Ocean/OceanData.h"
#include <string>
#include <cstdio>
#ifdef AQ_DEBUG_IMGUI
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

			// 永続フィールドの列挙（JSON 保存/読込）。常時コンパイル。
			// ImGui 編集は Inspect 側（reinit のコミット制御が必要なため別実装）。
			template <typename V>
			void Reflect(V& visitor)
			{
				int resolution = static_cast<int>(params_.resolution);
				visitor.Field("size",       params_.size, "Size");
				visitor.Field("resolution", resolution,   "Resolution");
				params_.resolution = static_cast<uint32_t>(resolution);   // read 時に反映（write 時は同値）

				visitor.FieldPath("normalMap1", normalMapPath1_, "Normal Map 1");
				visitor.FieldPath("normalMap2", normalMapPath2_, "Normal Map 2");
				visitor.Field("normalScale1", params_.normalScale1);
				visitor.Field("normalDirX1",  params_.normalDirX1);
				visitor.Field("normalDirZ1",  params_.normalDirZ1);
				visitor.Field("normalSpeed1", params_.normalSpeed1);
				visitor.Field("normalScale2", params_.normalScale2);
				visitor.Field("normalDirX2",  params_.normalDirX2);
				visitor.Field("normalDirZ2",  params_.normalDirZ2);
				visitor.Field("normalSpeed2", params_.normalSpeed2);

				visitor.Field("deepColor",    params_.deepColor,    "Deep Color");
				visitor.Field("shallowColor", params_.shallowColor, "Shallow Color");
				visitor.Field("skyColor",     params_.skyColor,     "Sky Color");
				visitor.Field("fresnelBias",  params_.fresnelBias);
				visitor.Field("fresnelScale", params_.fresnelScale);
				visitor.Field("fresnelPower", params_.fresnelPower);
				visitor.Field("sunShininess", params_.sunShininess);
				visitor.Field("sunIntensity", params_.sunIntensity);
				visitor.Field("waveQ",        params_.waveQ);

				for (int i = 0; i < 4; ++i)
				{
					char k[24];
					sprintf_s(k, sizeof(k), "wave%d_dirX",    i); visitor.Field(k, params_.waves[i].dirX);
					sprintf_s(k, sizeof(k), "wave%d_dirZ",    i); visitor.Field(k, params_.waves[i].dirZ);
					sprintf_s(k, sizeof(k), "wave%d_amp",     i); visitor.Field(k, params_.waves[i].amplitude);
					sprintf_s(k, sizeof(k), "wave%d_wavelen", i); visitor.Field(k, params_.waves[i].wavelength);
					sprintf_s(k, sizeof(k), "wave%d_speed",   i); visitor.Field(k, params_.waves[i].speed);
				}
			}

			// deserialize 後に呼ぶ。読み込んだ params で海を再構築する。
			void OnDeserialized()
			{
				params_.normalMapPath1 = normalMapPath1_.empty() ? nullptr : normalMapPath1_.c_str();
				params_.normalMapPath2 = normalMapPath2_.empty() ? nullptr : normalMapPath2_.c_str();
				Initialize(params_);
			}

		private:
			enum class State : uint8_t { Invalid, Completed };
			State              state_  = State::Invalid;
			ocean::OceanParams params_;
			ocean::OceanMesh   mesh_;
			std::string        normalMapPath1_;
			std::string        normalMapPath2_;
		};
	}
}
