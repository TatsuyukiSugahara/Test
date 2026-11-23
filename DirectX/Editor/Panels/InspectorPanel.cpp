#include "stdafx.h"
#include "InspectorPanel.h"
#include "../EditorContext.h"
#include "../Commands/ComponentCommands.h"
#include "Core/AppHost.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/ImGuiEditEventVisitor.h"
#include "ECS/SceneHierarchySystem.h"
#include "Level/LevelEditModel.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>


namespace editor
{
	namespace
	{
		/** category を持たない型の分類 */
		static constexpr const char* OTHER_CATEGORY = "Other";

		/** 失敗の表示色 */
		static constexpr ImVec4 ERROR_TEXT_COLOR = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);


		/** Add Component のパレットでの分類 */
		const char* CategoryOf(const aq::ecs::ComponentMeta& meta)
		{
			return (meta.category != nullptr) ? meta.category : OTHER_CATEGORY;
		}


		/** 見出しに出す名前 */
		const char* LabelOf(const aq::ecs::ComponentMeta* meta, const std::string& typeName)
		{
			if (meta != nullptr && meta->displayName != nullptr) return meta->displayName;
			return typeName.empty() ? "Component" : typeName.c_str();
		}


		/** node が type のコンポーネントを持つか */
		bool HasComponent(const aq::ecs::PrefabEditNode& node, const aq::ecs::TypeInfo& type)
		{
			for (const aq::ecs::PrefabEditComponent& component : node.components) {
				if (component.data.Type() == type) return true;
			}
			return false;
		}


		/** Add Component に出す型か(持っている型は出さない。写しと保存のために型消去の関数が要る) */
		bool CanAdd(const aq::ecs::PrefabEditNode& node, const aq::ecs::TypeInfo& type, const aq::ecs::ComponentMeta& meta)
		{
			if (!meta.addable || meta.typeName == nullptr) return false;
			if (!meta.serializePtr || !meta.deserializePtr) return false;
			return !HasComponent(node, type);
		}
	}


	void InspectorPanel::Draw(EditorContext& context)
	{
		// 描いたものの記録はフレームごとに取り直す
		drawnNodeId_ = 0;
		drawnTypes_.clear();
		isNameDrawn_ = false;

		if (ImGui::Begin(WINDOW_NAME)) {
			const aq::level::LevelEditModel* model = context.GetModel();
			// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
			if (aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) {
				// 再生中は実行中のワールドを出す。編集モデルは出さない。構造の変更は出さない
				ImGui::TextUnformatted("Playing: changes are not saved");
#ifdef AQ_DEBUG_IMGUI
				if (aq::ecs::SceneHierarchySystem::IsAvailable()) {
					aq::ecs::SceneHierarchySystem::Get().DrawInspectorContents(false);
				}
#endif
			} else if (model == nullptr) {
				ImGui::TextUnformatted("Level editor is not available");
			} else {
				const uint32_t                 nodeId = context.GetSelectedId();
				const aq::ecs::PrefabEditNode* node   = (nodeId != 0) ? model->FindNode(nodeId) : nullptr;
				if (node == nullptr) {
					ImGui::TextUnformatted("No selection");
				} else {
					DrawNode(context, *model, *node, nodeId);
				}
			}
		}
		ImGui::End();

		// 名前の入力が描かれなかった(選択を外した・ウィンドウが閉じた・隠れた)ら、入力中の名前を確定する
		if (!isNameDrawn_) {
			FinishNameEdit(context);
			nameNodeId_ = 0;
		}

		// 開いている操作のフィールドが描かれなかったら、控えと写しを捨てる
		DiscardUndrawnEdits();
	}


	void InspectorPanel::DrawNode(EditorContext& context, const aq::level::LevelEditModel& model, const aq::ecs::PrefabEditNode& node,
	                              const uint32_t nodeId)
	{
		drawnNodeId_ = nodeId;

		// 選択が変わったら、前のノードの開いている操作はもう描かれない。写しを作り直す前に捨てる
		const bool hasOtherNodeEdit = std::any_of(openEdits_.begin(), openEdits_.end(), [nodeId](const OpenEdit& edit)
			{
				return edit.nodeId != nodeId;
			});
		if (hasOtherNodeEdit) {
			openEdits_.erase(std::remove_if(openEdits_.begin(), openEdits_.end(), [nodeId](const OpenEdit& edit)
				{
					return edit.nodeId != nodeId;
				}), openEdits_.end());
			copiesValid_ = false;
		}

		// 写しは選択が変わった・モデルが変わったときだけ作り直す(参照ノードはファイルを読むので毎フレームは解決しない)。
		// 操作が開いている間は作り直さない(写しを書き換えている最中なので、作り直すと操作の途中の値と控えが食い違う)
		const bool needsRebuild = !copiesValid_ || copiesNodeId_ != nodeId || copiesVersion_ != context.GetVersion();
		if (needsRebuild && openEdits_.empty()) {
			RebuildCopies(context, model, node, nodeId);
		}

		const bool isReference = !node.prefabRef.empty();

		// 別のノードの同じフィールドと ID が重ならないように、ノード ID の下で描く
		ImGui::PushID(static_cast<int>(nodeId));

		DrawName(context, node, nodeId);
		if (isReference) {
			// 参照先の差し替えはまだ作っていないので、読み取り専用で出す
			ImGui::LabelText("Prefab", "%s", node.prefabRef.c_str());
		}
		ImGui::Separator();

		if (copiesNodeId_ != nodeId) {
			// 写しが別のノードのもの(作り直しを待っている)なら中身を出さない
			ImGui::TextDisabled("Loading...");
		} else if (isReference && resolveFailed_) {
			ImGui::TextColored(ERROR_TEXT_COLOR, "Cannot read referenced prefab");
			if (!resolveError_.empty()) {
				ImGui::TextWrapped("%s", resolveError_.c_str());
			}
		} else {
			DrawComponents(context, node, nodeId);
			// 参照ノードのコンポーネントの追加・削除は override の編集になるので、ここでは出さない
			if (!isReference) {
				ImGui::Separator();
				DrawAddComponent(context, node, nodeId);
			}
		}

		ImGui::PopID();
	}


	void InspectorPanel::DrawName(EditorContext& context, const aq::ecs::PrefabEditNode& node, const uint32_t nodeId)
	{
		if (nameNodeId_ != nodeId) {
			// 対象が変わった。前のノードの入力中の名前を確定してから、新しいノードの名前を入れる
			FinishNameEdit(context);
			nameNodeId_ = nodeId;
			std::snprintf(nameBuffer_, sizeof(nameBuffer_), "%s", node.name.c_str());
		} else if (!isNameActive_) {
			// 入力中でなければモデルの名前に合わせる(コマンドの反映を表示に出す)
			std::snprintf(nameBuffer_, sizeof(nameBuffer_), "%s", node.name.c_str());
		}

		ImGui::InputText("Name", nameBuffer_, sizeof(nameBuffer_));
		isNameActive_ = ImGui::IsItemActive();
		isNameDrawn_  = true;

		// Enter か項目から外れたとき(Escape では入力前の文字に戻るので、変わっていない扱いになる)
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			const std::string newName = nameBuffer_;
			if (!newName.empty() && newName != node.name) {
				context.Submit(std::make_unique<RenameNodeCommand>(nodeId, node.name, newName));
			}
		}
	}


	void InspectorPanel::FinishNameEdit(EditorContext& context)
	{
		if (!isNameActive_) return;
		isNameActive_ = false;

		// 入力中に描かれなくなると ImGui は外れたことを知らせないので、ここで確定する
		const aq::level::LevelEditModel* model = context.GetModel();
		if (model == nullptr || nameNodeId_ == 0) return;
		const aq::ecs::PrefabEditNode* node = model->FindNode(nameNodeId_);
		if (node == nullptr) return;

		const std::string newName = nameBuffer_;
		if (!newName.empty() && newName != node->name) {
			context.Submit(std::make_unique<RenameNodeCommand>(nameNodeId_, node->name, newName));
		}
	}


	void InspectorPanel::DrawComponents(EditorContext& context, const aq::ecs::PrefabEditNode& node, const uint32_t nodeId)
	{
		const aq::ecs::ComponentRegistry& registry = aq::ecs::ComponentRegistry::Get();
		const bool isReference = !node.prefabRef.empty();

		if (copies_.empty()) {
			ImGui::TextDisabled("No components");
		}

		std::vector<aq::ecs::FieldEditEvent> events;
		for (ComponentCopy& copy : copies_) {
			// 型の情報は描くたびに引き直す(写しには持たない)
			const aq::ecs::ComponentMeta* meta = copy.typeName.empty() ? nullptr : registry.Find(copy.typeName);

			ImGui::PushID(copy.typeName.c_str());
			const bool isOpen = ImGui::CollapsingHeader(LabelOf(meta, copy.typeName), ImGuiTreeNodeFlags_DefaultOpen);

			// 取り外し(ヘッダの右クリック)。自動で付く型(addable == false)と参照ノードでは出さない
			if (!isReference && meta != nullptr && meta->addable && ImGui::BeginPopupContextItem("ComponentContext")) {
				if (ImGui::MenuItem("Remove Component")) {
					context.Submit(std::make_unique<RemoveComponentCommand>(nodeId, copy.typeName));
				}
				ImGui::EndPopup();
			}

			if (isOpen) {
				if (meta != nullptr && meta->drawEditPtr && copy.data.IsValid()) {
					events.clear();
					meta->drawEditPtr(copy.data.Get(), events);
					drawnTypes_.push_back(copy.typeName);
					HandleEditEvents(context, nodeId, copy.typeName, events);
				} else if (!copy.readOnlyText.empty()) {
					// 編集イベントを出せない型は、写しの値を読み取り専用で出す
					ImGui::TextDisabled("Read only");
					ImGui::TextUnformatted(copy.readOnlyText.c_str());
				} else {
					ImGui::TextDisabled("Cannot be edited");
				}
			}
			ImGui::PopID();
		}
	}


	void InspectorPanel::DrawAddComponent(EditorContext& context, const aq::ecs::PrefabEditNode& node, const uint32_t nodeId)
	{
		if (ImGui::Button("Add Component")) {
			ImGui::OpenPopup("AddComponentPopup");
		}
		if (!ImGui::BeginPopup("AddComponentPopup")) return;

		const aq::ecs::ComponentRegistry& registry = aq::ecs::ComponentRegistry::Get();

		// 足せる型の分類を登録順に集める("Other" は最後)
		std::vector<std::string> categories;
		bool hasOther = false;
		for (const auto& [type, meta] : registry.GetAll()) {
			if (!CanAdd(node, type, meta)) continue;
			const std::string category = CategoryOf(meta);
			if (category == OTHER_CATEGORY) {
				hasOther = true;
			} else if (std::find(categories.begin(), categories.end(), category) == categories.end()) {
				categories.push_back(category);
			}
		}
		if (hasOther) {
			categories.push_back(OTHER_CATEGORY);
		}

		if (categories.empty()) {
			ImGui::TextDisabled("No components to add");
		}
		for (const std::string& category : categories) {
			if (!ImGui::BeginMenu(category.c_str())) continue;
			for (const auto& [type, meta] : registry.GetAll()) {
				if (!CanAdd(node, type, meta) || category != CategoryOf(meta)) continue;
				if (ImGui::MenuItem(LabelOf(&meta, meta.typeName))) {
					context.Submit(std::make_unique<AddComponentCommand>(nodeId, meta.typeName));
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}


	void InspectorPanel::RebuildCopies(const EditorContext& context, const aq::level::LevelEditModel& model,
	                                   const aq::ecs::PrefabEditNode& node, const uint32_t nodeId)
	{
		copies_.clear();
		copiesNodeId_  = nodeId;
		copiesVersion_ = context.GetVersion();
		copiesValid_   = true;
		resolveFailed_ = false;
		resolveError_.clear();

		const aq::ecs::ComponentRegistry& registry = aq::ecs::ComponentRegistry::Get();

		// 実体ノード: モデルのコンポーネントを JSON にして、新しい実体へ読み込む(モデルの実体には書かない)
		if (node.prefabRef.empty()) {
			for (const aq::ecs::PrefabEditComponent& component : node.components) {
				const aq::ecs::ComponentMeta* meta = registry.Find(component.data.Type().GetHash());

				ComponentCopy copy;
				copy.typeName = (meta != nullptr && meta->typeName != nullptr) ? std::string(meta->typeName) : std::string();
				if (meta != nullptr && meta->serializePtr && meta->deserializePtr && component.data.IsValid()) {
					aq::util::JsonValue json;
					meta->serializePtr(component.data.Get(), json);
					copy.data = aq::ecs::AlignedStorage(component.data.Type());
					meta->deserializePtr(copy.data.Get(), json);

					if (!meta->drawEditPtr) {
						aq::util::JsonValue copyJson;
						meta->serializePtr(copy.data.Get(), copyJson);
						copy.readOnlyText = aq::util::JsonSerializer::Stringify(copyJson);
					}
				}
				copies_.push_back(std::move(copy));
			}
			return;
		}

		// 参照ノード: 参照先の展開と override の適用を済ませた値から作る(ファイルを読むので作り直すときだけ)
		aq::util::JsonValue resolved;
		std::string         error;
		if (!model.ResolveReferenceComponents(nodeId, resolved, &error)) {
			resolveFailed_ = true;
			resolveError_  = error;
			return;
		}

		// 並びは登録順にする(解決結果のオブジェクトは順序を持たないので、フレームごとに並びが変わらないように)
		std::vector<std::string> usedTypeNames;
		for (const auto& [type, meta] : registry.GetAll()) {
			if (meta.typeName == nullptr || !resolved.Contains(meta.typeName)) continue;
			const aq::util::JsonValue& json = resolved[meta.typeName];

			ComponentCopy copy;
			copy.typeName = meta.typeName;
			if (meta.serializePtr && meta.deserializePtr) {
				copy.data = aq::ecs::AlignedStorage(type);
				meta.deserializePtr(copy.data.Get(), json);
			}
			if (!meta.drawEditPtr) {
				aq::util::JsonValue copyJson;
				if (copy.data.IsValid()) {
					meta.serializePtr(copy.data.Get(), copyJson);
				} else {
					copyJson = json;
				}
				copy.readOnlyText = aq::util::JsonSerializer::Stringify(copyJson);
			}
			usedTypeNames.push_back(copy.typeName);
			copies_.push_back(std::move(copy));
		}

		// 登録されていない型は、解決した JSON をそのまま読み取り専用で出す(名前順)
		std::vector<std::string> unknownTypeNames;
		for (const auto& entry : resolved.GetObject()) {
			if (std::find(usedTypeNames.begin(), usedTypeNames.end(), entry.first) != usedTypeNames.end()) continue;
			unknownTypeNames.push_back(entry.first);
		}
		std::sort(unknownTypeNames.begin(), unknownTypeNames.end());
		for (const std::string& typeName : unknownTypeNames) {
			ComponentCopy copy;
			copy.typeName     = typeName;
			copy.readOnlyText = aq::util::JsonSerializer::Stringify(resolved[typeName]);
			copies_.push_back(std::move(copy));
		}
	}


	void InspectorPanel::HandleEditEvents(EditorContext& context, const uint32_t nodeId, const std::string& typeName,
	                                      std::vector<aq::ecs::FieldEditEvent>& events)
	{
		for (aq::ecs::FieldEditEvent& event : events) {
			switch (event.phase) {
			case aq::ecs::FieldEditPhase::Begin:
			{
				// 同じフィールドの古い控えが残っていれば置き換える
				std::vector<OpenEdit>::iterator found = FindOpenEdit(nodeId, typeName, event.key);
				if (found != openEdits_.end()) {
					openEdits_.erase(found);
				}
				OpenEdit edit;
				edit.nodeId   = nodeId;
				edit.typeName = typeName;
				edit.key      = event.key;
				edit.before   = std::move(event.value);
				openEdits_.push_back(std::move(edit));
				break;
			}

			case aq::ecs::FieldEditPhase::Update:
				// ドラッグ中の値はモデルへ送らない(確定したときだけコマンドにする)
				break;

			case aq::ecs::FieldEditPhase::Commit:
			{
				// 操作を閉じてコマンドにする。変更前の値はコマンドが実行の時点のモデルから取るので、控えは渡さない
				// (控えが無い(起きないはず)ときも、値はモデルへ送る)
				std::vector<OpenEdit>::iterator found = FindOpenEdit(nodeId, typeName, event.key);
				if (found != openEdits_.end()) {
					openEdits_.erase(found);
				}
				context.Submit(std::make_unique<SetFieldCommand>(nodeId, typeName, event.key, event.value));
				// 次のフレームに反映後のモデルから作り直す(コマンドが失敗しても、写しに確定していない値を残さない)
				copiesValid_ = false;
				break;
			}

			case aq::ecs::FieldEditPhase::Cancel:
			{
				std::vector<OpenEdit>::iterator found = FindOpenEdit(nodeId, typeName, event.key);
				if (found != openEdits_.end()) {
					openEdits_.erase(found);
				}
				break;
			}
			}
		}
	}


	void InspectorPanel::DiscardUndrawnEdits()
	{
		const size_t before = openEdits_.size();
		openEdits_.erase(std::remove_if(openEdits_.begin(), openEdits_.end(), [this](const OpenEdit& edit)
			{
				if (edit.nodeId != drawnNodeId_) return true;
				return std::find(drawnTypes_.begin(), drawnTypes_.end(), edit.typeName) == drawnTypes_.end();
			}), openEdits_.end());

		// 操作中に写しは書き換わっているので、控えだけを捨てると確定していない値が残って表示され、
		// 次の Begin もその値を変更前として控えてしまう。写しも捨てて次のフレームで作り直す
		if (openEdits_.size() != before) {
			copiesValid_ = false;
		}
	}


	std::vector<InspectorPanel::OpenEdit>::iterator InspectorPanel::FindOpenEdit(const uint32_t nodeId, const std::string& typeName,
	                                                                             const std::string& key)
	{
		return std::find_if(openEdits_.begin(), openEdits_.end(), [&](const OpenEdit& edit)
			{
				return edit.nodeId == nodeId && edit.typeName == typeName && edit.key == key;
			});
	}
}
