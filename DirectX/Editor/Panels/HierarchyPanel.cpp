#include "stdafx.h"
#include "HierarchyPanel.h"
#include "../EditorContext.h"
#include "../Commands/NodeCommands.h"
#include "../PrefabAuthoring.h"
#include "Core/AppHost.h"
#include "Editor/AssetDragDrop.h"
#include "ECS/SceneHierarchySystem.h"
#include "Level/LevelEditModel.h"
#include <imgui/imgui.h>
#include <string>


namespace editor
{
	namespace
	{
		/** 新しく作る実体ノードの名前 */
		static constexpr const char* NEW_ENTITY_NAME = "Entity";
	}


	void HierarchyPanel::Draw(EditorContext& context)
	{
		if (ImGui::Begin(WINDOW_NAME)) {
			const aq::level::LevelEditModel* model = context.GetModel();
			// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
			if (aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) {
				// 再生中は実行中のワールドを出す。編集モデルの木は出さない。構造の変更は出さない
				ImGui::TextUnformatted("Playing: changes are not saved");
#ifdef AQ_DEBUG_IMGUI
				if (aq::ecs::SceneHierarchySystem::IsAvailable()) {
					aq::ecs::SceneHierarchySystem::Get().DrawHierarchyContents(false);
				}
#endif
			} else if (model == nullptr) {
				ImGui::TextUnformatted("Level editor is not available");
			} else {
				DrawModel(context, *model);
			}
		}
		ImGui::End();
	}


	void HierarchyPanel::DrawModel(EditorContext& context, const aq::level::LevelEditModel& model)
	{
		aq::ecs::PrefabTreeDrawOptions options;
		options.selected      = (context.GetSelectedId() != 0) ? model.FindNode(context.GetSelectedId()) : nullptr;
		options.root          = nullptr;   // どのノードも削除できる(ルート列の要素も消してよい)
		options.enableMove    = true;
		options.allowRefChild = false;     // 参照ノードの下には作らない・移さない(override の編集は別に作る)
		options.markReference = true;
		options.acceptPrefabDrop = true;   // コンテンツブラウザの Prefab を落とすと、そのノードの子の末尾に参照ノードを作る

		std::vector<aq::ecs::PrefabNodeOp> ops;

		// トップレベルの entities
		ImGui::PushID("TopLevel");
		DrawRootHeader(context, model, model.GetName().c_str(), -1);
		DrawNodeList(model.GetEntities(), options, ops);
		ImGui::PopID();

		// 1 段目の subLevel。インラインはその entities の木、ファイル参照はパスだけを無効表示する
		const std::vector<aq::level::SubLevelEdit>& subLevels = model.GetSubLevels();
		if (!subLevels.empty()) {
			ImGui::Spacing();
			ImGui::SeparatorText("Sub Levels");
			for (size_t i = 0; i < subLevels.size(); ++i) {
				const aq::level::SubLevelEdit& subLevel = subLevels[i];
				ImGui::PushID(static_cast<int>(i));
				if (subLevel.isInline) {
					DrawRootHeader(context, model, subLevel.name.c_str(), static_cast<int>(i));
					DrawNodeList(subLevel.entities, options, ops);
				} else {
					ImGui::TextDisabled("%s (file)", subLevel.path.c_str());
				}
				ImGui::PopID();
			}
		}

		// 要求のポインタは次にモデルが変わるまでしか有効でないので、このフレームのうちに ID に直す
		ApplyTreeOps(context, ops);
	}


	void HierarchyPanel::DrawRootHeader(EditorContext& context, const aq::level::LevelEditModel& model, const char* label,
	                                    const int subLevelIndex)
	{
		// 横の "+" の分だけ幅を空ける
		const ImGuiStyle& style = ImGui::GetStyle();
		const float buttonWidth = ImGui::CalcTextSize("+").x + style.FramePadding.x * 2.0f;
		const float headerWidth = std::max(ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x, 1.0f);

		// 名前が空でも ID が決まるように、見えない接尾辞を付ける
		const std::string headerLabel = std::string(label) + "##RootHeader";
		ImGui::Selectable(headerLabel.c_str(), false, ImGuiSelectableFlags_None, ImVec2(headerWidth, 0.0f));

		// 見出しへ落としたノードは、その列の末尾へ移す
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(aq::ecs::PREFAB_NODE_DRAG_PAYLOAD)) {
				if (payload->DataSize == static_cast<int>(sizeof(const aq::ecs::PrefabEditNode*))) {
					// 荷物はドラッグを始めたフレームのポインタなので、今のモデルにあるノードかを確かめてから読む
					const aq::ecs::PrefabEditNode* node = *static_cast<const aq::ecs::PrefabEditNode* const*>(payload->Data);
					if (EditorContext::ContainsNode(model, node) && node->editId != 0) {
						NodeParent parent;
						parent.nodeId        = 0;
						parent.subLevelIndex = subLevelIndex;
						context.Submit(std::make_unique<MoveNodeCommand>(node->editId, parent, NODE_INDEX_APPEND));
					}
				}
			}
			// コンテンツブラウザの Prefab は、その列の末尾に参照ノードとして作る
			std::string assetPath;
			if (aq::editor::AcceptAssetDragPayload(aq::ecs::AssetKind::Prefab, assetPath)) {
				SubmitCreateReference(context, model, 0, subLevelIndex, assetPath);
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("RootHeaderContext")) {
			if (ImGui::MenuItem("Create Entity")) {
				SubmitCreateAtRoot(context, subLevelIndex);
			}
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("+")) {
			SubmitCreateAtRoot(context, subLevelIndex);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Create Entity");
		}
	}


	void HierarchyPanel::DrawNodeList(const NodeList& list, const aq::ecs::PrefabTreeDrawOptions& options,
	                                  std::vector<aq::ecs::PrefabNodeOp>& ops)
	{
		for (const std::unique_ptr<aq::ecs::PrefabEditNode>& node : list) {
			aq::ecs::PrefabNodeDrawTree(node.get(), options, 0, ops);
		}
	}


	void HierarchyPanel::ApplyTreeOps(EditorContext& context, const std::vector<aq::ecs::PrefabNodeOp>& ops)
	{
		for (const aq::ecs::PrefabNodeOp& op : ops) {
			if (op.target == nullptr || op.target->editId == 0) continue;
			const uint32_t targetId = op.target->editId;

			switch (op.kind) {
			case aq::ecs::PrefabNodeOpKind::Select:
				context.SetSelectedId(targetId);
				break;

			case aq::ecs::PrefabNodeOpKind::AddChild:
			{
				NodeParent parent;
				parent.nodeId = targetId;
				context.Submit(std::make_unique<CreateNodeCommand>(parent, NODE_INDEX_APPEND, NEW_ENTITY_NAME));
				break;
			}

			case aq::ecs::PrefabNodeOpKind::Delete:
				// 選択が消えるノードの中にあれば、実行後に EditorContext が外す
				context.Submit(std::make_unique<DeleteNodeCommand>(targetId));
				break;

			case aq::ecs::PrefabNodeOpKind::DropPrefab:
			{
				const aq::level::LevelEditModel* model = context.GetModel();
				if (model == nullptr) break;
				SubmitCreateReference(context, *model, targetId, -1, op.assetPath);
				break;
			}

			case aq::ecs::PrefabNodeOpKind::Move:
			{
				if (op.newParent == nullptr || op.newParent->editId == 0) break;
				NodeParent parent;
				parent.nodeId = op.newParent->editId;
				context.Submit(std::make_unique<MoveNodeCommand>(targetId, parent, NODE_INDEX_APPEND));
				break;
			}
			}
		}
	}


	void HierarchyPanel::SubmitCreateReference(EditorContext& context, const aq::level::LevelEditModel& model,
	                                           const uint32_t parentNodeId, const int subLevelIndex, const std::string& assetPath)
	{
		if (assetPath.empty()) return;
		NodeParent parent;
		parent.nodeId        = parentNodeId;
		parent.subLevelIndex = subLevelIndex;
		context.Submit(std::make_unique<CreateReferenceNodeCommand>(
			parent, NODE_INDEX_APPEND, PrefabAuthoring::ReferenceNameFromPath(assetPath),
			PrefabAuthoring::MakeReferencePath(model.GetBaseDir(), assetPath)));
	}


	void HierarchyPanel::SubmitCreateAtRoot(EditorContext& context, const int subLevelIndex)
	{
		NodeParent parent;
		parent.nodeId        = 0;
		parent.subLevelIndex = subLevelIndex;
		context.Submit(std::make_unique<CreateNodeCommand>(parent, NODE_INDEX_APPEND, NEW_ENTITY_NAME));
	}
}
