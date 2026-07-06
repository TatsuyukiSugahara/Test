#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "PrefabEditor.h"
#include "PrefabEditNodeOps.h"
#include "PrefabSerializer.h"
#include "Prefab.h"
#include "EntityContext.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace ecs
	{
		PrefabEditorPanel::PrefabEditorPanel()
		{
			NewPrefab();
		}


		void PrefabEditorPanel::NewPrefab()
		{
			root_ = std::make_unique<PrefabEditNode>();
			root_->name = "Root";
			selected_   = root_.get();
		}


		// ── メニュー / エントリ ───────────────────────────────────────────────────

		void PrefabEditorPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Prefab Editor", nullptr, &show_);
		}


		void PrefabEditorPanel::DebugRender()
		{
			if (!show_) return;

			ImGui::SetNextWindowSize({ 720.0f, 520.0f }, ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Prefab Editor", &show_))
			{
				ImGui::End();
				return;
			}

			DrawToolbar();
			ImGui::Separator();

			// 左: 階層ツリー / 右: 選択ノードのインスペクター
			const float leftW = 240.0f;
			ImGui::BeginChild("##prefab_tree", ImVec2(leftW, 0.0f), true);
			if (root_) DrawTree(root_.get(), 0);
			ImGui::EndChild();

			// 描画後に削除予約ノードを除去する（ImGui スタックに影響しない安全なタイミング）。
			if (pendingDelete_)
			{
				if (selected_ == pendingDelete_) selected_ = root_.get();
				RemoveNode(root_.get(), pendingDelete_);
				pendingDelete_ = nullptr;
			}

			ImGui::SameLine();

			ImGui::BeginChild("##prefab_inspector", ImVec2(0.0f, 0.0f), true);
			DrawInspector();
			ImGui::EndChild();

			ImGui::End();
		}


		// ── ツールバー ───────────────────────────────────────────────────────────

		void PrefabEditorPanel::DrawToolbar()
		{
			if (ImGui::Button("New")) NewPrefab();
			ImGui::SameLine();
			if (ImGui::Button("Load")) Load();
			ImGui::SameLine();
			if (ImGui::Button("Save")) Save();
			ImGui::SameLine();
			if (ImGui::Button("Spawn Preview")) SpawnPreview();

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##path", pathBuf_, sizeof(pathBuf_));
		}


		// ── 階層ツリー ───────────────────────────────────────────────────────────

		void PrefabEditorPanel::DrawTree(PrefabEditNode* node, int depth)
		{
			PrefabNodeDrawTree(node, selected_, pendingDelete_, root_.get(), depth);
		}


		bool PrefabEditorPanel::RemoveNode(PrefabEditNode* parent, PrefabEditNode* target)
		{
			return PrefabNodeRemove(parent, target);
		}


		// ── インスペクター ───────────────────────────────────────────────────────

		void PrefabEditorPanel::DrawInspector()
		{
			if (!selected_)
			{
				ImGui::TextDisabled("No node selected.");
				return;
			}
			PrefabNodeDrawInspector(*selected_, selected_);
		}


		// ── 編集ツリー ⇄ JSON（共有オペレーションへ委譲）───────────────────────────

		util::JsonValue PrefabEditorPanel::NodeToJson(const PrefabEditNode& node) const
		{
			return PrefabNodeToJson(node);
		}


		std::unique_ptr<PrefabEditNode> PrefabEditorPanel::JsonToNode(const util::JsonValue& json) const
		{
			return PrefabNodeFromJson(json);
		}


		// ── Save / Load / Spawn ───────────────────────────────────────────────────

		void PrefabEditorPanel::Save()
		{
			if (!root_) return;
			const util::JsonValue json = NodeToJson(*root_);
			if (!util::JsonSerializer::WriteFile(pathBuf_, json))
				EngineAssertMsg(false, "PrefabEditor::Save: failed to write prefab JSON file");
		}


		void PrefabEditorPanel::Load()
		{
			const util::JsonValue json = util::JsonParser::ParseFile(pathBuf_);
			if (json.IsNull())
			{
				EngineAssertMsg(false, "PrefabEditor::Load: failed to parse prefab JSON file");
				return;
			}
			root_     = JsonToNode(json);
			selected_ = root_.get();
		}


		void PrefabEditorPanel::SpawnPreview()
		{
			if (!root_) return;
			// 編集ツリー → JSON → ランタイム Prefab（FromJson）→ 遅延生成。Reflect 経路を共有する。
			const util::JsonValue json = NodeToJson(*root_);
			Prefab prefab = PrefabSerializer::FromJson(json);
			if (prefab.IsValid())
				prefab.Instantiate();
		}
	}
}
#endif
