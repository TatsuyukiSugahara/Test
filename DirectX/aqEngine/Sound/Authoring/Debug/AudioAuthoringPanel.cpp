#include "aq.h"
#include "AudioAuthoringPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include <vector>
#include <utility>
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
						ImGui::TableNextColumn(); ImGui::TextUnformatted(v.isStream ? "stream" : "one-shot");
					}
					ImGui::EndTable();
				}
			}

			// ── Switch 状態（グローバル）──
			if (ImGui::CollapsingHeader("Switches (global)", ImGuiTreeNodeFlags_DefaultOpen)) {
				std::vector<std::pair<NameId, NameId>> sw;
				dir.GetGlobalSwitches(sw);
				if (sw.empty()) {
					ImGui::TextDisabled("(none set)");
				}
				for (const auto& kv : sw) {
					ImGui::BulletText("%s = %s", dir.NameOf(kv.first).c_str(), dir.NameOf(kv.second).c_str());
				}
				ImGui::Separator();
				ImGui::TextUnformatted("Surface:");
				ImGui::SameLine();
				if (ImGui::SmallButton("Grass")) { dir.SetSwitch(static_cast<NameId>(aq::util::ComputeCrc32("Surface")),
				                                                 static_cast<NameId>(aq::util::ComputeCrc32("Grass"))); }
				ImGui::SameLine();
				if (ImGui::SmallButton("Wood"))  { dir.SetSwitch(static_cast<NameId>(aq::util::ComputeCrc32("Surface")),
				                                                 static_cast<NameId>(aq::util::ComputeCrc32("Wood"))); }
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
