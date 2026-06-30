#include "aq.h"
#include "AudioAuthoringPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include <vector>
#include <utility>
#include <unordered_set>
#include <string>
#include <cstdio>
#include <cfloat>
#include "Sound/Authoring/AudioDirector.h"
#include "Sound/SoundEngine.h"


namespace aq
{
	namespace audio
	{
		namespace
		{
			const char* BusName(sound::SoundBusId bus)
			{
				switch (bus) {
				case sound::SoundBusId::Master: return "Master";
				case sound::SoundBusId::BGM:    return "BGM";
				case sound::SoundBusId::SE:     return "SE";
				case sound::SoundBusId::Voice:  return "Voice";
				default:                        return "?";
				}
			}

			const char* ObjectTypeName(ObjectType t)
			{
				switch (t) {
				case ObjectType::Sound:    return "Sound";
				case ObjectType::Random:   return "Random";
				case ObjectType::Sequence: return "Sequence";
				case ObjectType::Switch:   return "Switch";
				case ObjectType::Blend:    return "Blend";
				default:                   return "?";
				}
			}

			const char* PropertyName(RtpcProperty p)
			{
				return p == RtpcProperty::Pitch ? "pitch" : "volumeDb";
			}

			// カーブの線形評価（パネルのプロット用）。
			float EvalCurveLocal(const std::vector<CurvePoint>& c, float x)
			{
				if (c.empty()) { return 0.0f; }
				if (x <= c.front().x) { return c.front().y; }
				if (x >= c.back().x)  { return c.back().y; }
				for (size_t i = 1; i < c.size(); ++i) {
					if (x <= c[i].x) {
						const CurvePoint& p0 = c[i - 1];
						const CurvePoint& p1 = c[i];
						const float t = (p1.x != p0.x) ? (x - p0.x) / (p1.x - p0.x) : 0.0f;
						return p0.y + (p1.y - p0.y) * t;
					}
				}
				return c.back().y;
			}

			// オブジェクトツリーを再帰描画（Random/Sequence/Switch/Blend を展開）。
			void RenderObjectNode(AudioDirector& dir, NameId id, int& uid)
			{
				const SoundObjectDef* o = dir.GetObjectDef(id);
				std::string name = dir.NameOf(id);
				if (name.empty()) { name = "(anon)"; }

				ImGui::PushID(uid++);
				if (o == nullptr) {
					ImGui::BulletText("%s -> missing", name.c_str());
					ImGui::PopID();
					return;
				}

				if (o->type == ObjectType::Sound) {
					if (ImGui::SmallButton("Play")) { dir.DebugPlayObject(id); }
					ImGui::SameLine();
					ImGui::Text("%s  [Sound]  %s", name.c_str(), o->clip.c_str());
					ImGui::PopID();
					return;
				}

				char label[160];
				std::snprintf(label, sizeof(label), "%s  [%s]", name.c_str(), ObjectTypeName(o->type));
				const bool open = ImGui::TreeNode(label);
				ImGui::SameLine();
				if (ImGui::SmallButton("Play")) { dir.DebugPlayObject(id); }
				if (open) {
					switch (o->type) {
					case ObjectType::Random:
					case ObjectType::Sequence:
						for (NameId c : o->childIds) { RenderObjectNode(dir, c, uid); }
						break;
					case ObjectType::Switch: {
						const NameId cur = dir.GetCurrentSwitch(o->switchGroupId);
						for (const auto& m : o->switchMap) {
							const bool active = (m.first == cur);
							if (active) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f)); }
							ImGui::Text("%s%s ->", dir.NameOf(m.first).c_str(), active ? " *" : "");
							if (active) { ImGui::PopStyleColor(); }
							ImGui::Indent();
							RenderObjectNode(dir, m.second, uid);
							ImGui::Unindent();
						}
						break;
					}
					case ObjectType::Blend:
						for (const auto& layer : o->blendLayers) { RenderObjectNode(dir, layer.childId, uid); }
						break;
					default:
						break;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}


		void AudioAuthoringPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Audio", nullptr, &show_);
		}


		void AudioAuthoringPanel::DebugRender()
		{
			if (!show_) {
				return;
			}
			if (ImGui::Begin("Audio", &show_)) {
				RenderContent();
			}
			ImGui::End();
		}


		void AudioAuthoringPanel::RenderContent()
		{
			if (!AudioDirector::IsAvailable()) {
				ImGui::TextUnformatted("AudioDirector is not available.");
				return;
			}
			AudioDirector& dir = AudioDirector::Get();

			// ── 再生中ボイス ──
			if (ImGui::CollapsingHeader("Active Voices", ImGuiTreeNodeFlags_DefaultOpen)) {
				std::vector<AudioDirector::DebugVoiceInfo> voices;
				dir.CollectDebugVoices(voices);
				ImGui::Text("count: %d", static_cast<int>(voices.size()));
				if (ImGui::BeginTable("voices", 5,
						ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
					ImGui::TableSetupColumn("id");
					ImGui::TableSetupColumn("object");
					ImGui::TableSetupColumn("kind");
					ImGui::TableSetupColumn("bus");
					ImGui::TableSetupColumn("type");
					ImGui::TableHeadersRow();
					for (const auto& v : voices) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::Text("%u", v.id);
						ImGui::TableNextColumn(); ImGui::TextUnformatted(dir.NameOf(v.objectId).c_str());
						ImGui::TableNextColumn(); ImGui::TextUnformatted(dir.NameOf(v.kindId).c_str());
						ImGui::TableNextColumn(); ImGui::TextUnformatted(BusName(v.bus));
						ImGui::TableNextColumn();
						if (v.isVirtual)    { ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "virtual"); }
						else if (v.isStream) { ImGui::TextUnformatted("stream"); }
						else                 { ImGui::TextUnformatted("one-shot"); }
					}
					ImGui::EndTable();
				}
			}

			// ── State（グループごとに値ボタン。現在値をハイライト）──
			if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen)) {
				std::vector<std::pair<NameId, std::vector<NameId>>> groups;
				dir.CollectStateGroups(groups);
				if (groups.empty()) { ImGui::TextDisabled("(none)"); }
				int idBase = 0;
				for (const auto& g : groups) {
					ImGui::TextUnformatted(dir.NameOf(g.first).c_str());
					ImGui::SameLine();
					const NameId current = dir.GetCurrentState(g.first);
					for (NameId value : g.second) {
						ImGui::SameLine();
						ImGui::PushID(idBase++);
						const bool active = (value == current);
						if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f)); }
						if (ImGui::SmallButton(dir.NameOf(value).c_str())) { dir.SetState(g.first, value); }
						if (active) { ImGui::PopStyleColor(); }
						ImGui::PopID();
					}
				}
			}

			// ── Switch（グループごとに値ボタン。グローバル設定）──
			if (ImGui::CollapsingHeader("Switches", ImGuiTreeNodeFlags_DefaultOpen)) {
				std::vector<std::pair<NameId, std::vector<NameId>>> groups;
				dir.CollectSwitchGroups(groups);
				std::vector<std::pair<NameId, NameId>> current;
				dir.GetGlobalSwitches(current);
				auto curOf = [&current](NameId group) -> NameId {
					for (const auto& kv : current) { if (kv.first == group) return kv.second; }
					return 0;
				};
				if (groups.empty()) { ImGui::TextDisabled("(none)"); }
				int idBase = 1000;
				for (const auto& g : groups) {
					ImGui::TextUnformatted(dir.NameOf(g.first).c_str());
					const NameId cur = curOf(g.first);
					for (NameId value : g.second) {
						ImGui::SameLine();
						ImGui::PushID(idBase++);
						const bool active = (value == cur);
						if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f)); }
						if (ImGui::SmallButton(dir.NameOf(value).c_str())) { dir.SetSwitch(g.first, value); }
						if (active) { ImGui::PopStyleColor(); }
						ImGui::PopID();
					}
				}
			}

			// ── RTPC（ライブスライダ + カーブ表示）──
			if (ImGui::CollapsingHeader("RTPC", ImGuiTreeNodeFlags_DefaultOpen)) {
				std::vector<AudioDirector::RtpcInfo> rtpcs;
				dir.CollectRtpc(rtpcs);
				if (rtpcs.empty()) { ImGui::TextDisabled("(none)"); }
				int idBase = 2000;
				for (const auto& r : rtpcs) {
					ImGui::PushID(idBase++);
					float v = r.current;
					if (ImGui::SliderFloat(dir.NameOf(r.id).c_str(), &v, r.minV, r.maxV)) {
						dir.SetRTPC(r.id, v);
					}
					// この RTPC を使う binding のカーブをプロット。
					for (const AudioBank& bank : dir.GetBanks()) {
						for (const RtpcBindingDef& b : bank.GetRtpcBindings()) {
							if (b.rtpcId != r.id || b.curve.empty()) { continue; }
							float samples[48];
							for (int i = 0; i < 48; ++i) {
								const float x = r.minV + (r.maxV - r.minV) * (i / 47.0f);
								samples[i] = EvalCurveLocal(b.curve, x);
							}
							char label[96];
							std::snprintf(label, sizeof(label), "-> %s.%s", dir.NameOf(b.targetId).c_str(), PropertyName(b.property));
							ImGui::PlotLines(label, samples, 48, 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 40));
						}
					}
					ImGui::PopID();
				}
			}

			// ── オブジェクトツリー（コンテナ展開 + 任意再生 + Switch 解決ハイライト）──
			if (ImGui::CollapsingHeader("Objects (tree)")) {
				// 子として参照されている id を集め、未参照のものをルートとして描画する。
				std::unordered_set<NameId> referenced;
				for (const AudioBank& bank : dir.GetBanks()) {
					for (const auto& kv : bank.GetObjects()) {
						const SoundObjectDef& o = kv.second;
						for (NameId c : o.childIds) { referenced.insert(c); }
						for (const auto& m : o.switchMap) { referenced.insert(m.second); }
						for (const auto& layer : o.blendLayers) { referenced.insert(layer.childId); }
					}
				}
				int uid = 4000;
				for (const AudioBank& bank : dir.GetBanks()) {
					for (const auto& kv : bank.GetObjects()) {
						if (referenced.count(kv.first) == 0) {
							RenderObjectNode(dir, kv.first, uid);
						}
					}
				}
			}

			// ── イベントブラウザ（手動発火）──
			if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen)) {
				int buttonIndex = 0;
				for (const AudioBank& bank : dir.GetBanks()) {
					ImGui::Text("[%s]", bank.GetBankId().c_str());
					for (const auto& pair : bank.GetEvents()) {
						const NameId eventId = pair.first;
						const std::string name = dir.NameOf(eventId);
						ImGui::PushID(buttonIndex++);
						if (ImGui::Button("Post")) {
							dir.PostEvent(eventId);
						}
						ImGui::PopID();
						ImGui::SameLine();
						ImGui::TextUnformatted(name.empty() ? "(unnamed)" : name.c_str());
					}
				}
			}

			// ── バス音量 ──
			if (ImGui::CollapsingHeader("Buses")) {
				if (sound::SoundEngine::IsAvailable()) {
					sound::SoundEngine& se = sound::SoundEngine::Get();
					static float bgm = 1.0f, seVol = 1.0f, voice = 1.0f, master = 1.0f;
					if (ImGui::SliderFloat("Master", &master, 0.0f, 1.0f)) { se.SetMasterVolume(master); }
					if (ImGui::SliderFloat("BGM",    &bgm,    0.0f, 1.0f)) { se.SetBusVolume(sound::SoundBusId::BGM, bgm); }
					if (ImGui::SliderFloat("SE",     &seVol,  0.0f, 1.0f)) { se.SetBusVolume(sound::SoundBusId::SE, seVol); }
					if (ImGui::SliderFloat("Voice",  &voice,  0.0f, 1.0f)) { se.SetBusVolume(sound::SoundBusId::Voice, voice); }
				}
			}
		}
	}
}
#endif
