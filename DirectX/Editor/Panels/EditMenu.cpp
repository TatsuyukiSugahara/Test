#include "stdafx.h"
#include "EditMenu.h"
#include "../EditorContext.h"
#include "Core/AppHost.h"
#include <imgui/imgui.h>
#include <string>


namespace editor
{
	namespace
	{
		/** ショートカットの受け方(どのウィンドウにフォーカスがあっても効かせ、押し続けたら繰り返す) */
		static constexpr ImGuiInputFlags SHORTCUT_FLAGS = ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat;


		/** メニューの項目名。"Undo Set Field" のように次の操作の名前を付け、ID は ### 以降で固定する */
		std::string MakeItemLabel(const char* action, const char* commandName)
		{
			std::string label = action;
			if (commandName != nullptr) {
				label += " ";
				label += commandName;
			}
			label += "###";
			label += action;
			return label;
		}
	}


	void EditMenu::DrawMenu(EditorContext& context)
	{
		// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
		if (!ImGui::BeginMenu("Edit")) return;

		const bool enabled = IsEnabled(context);
		const std::string undoLabel = MakeItemLabel("Undo", context.GetUndoName());
		if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, enabled && context.CanUndo())) {
			context.RequestUndo();
		}
		const std::string redoLabel = MakeItemLabel("Redo", context.GetRedoName());
		if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, enabled && context.CanRedo())) {
			context.RequestRedo();
		}

		ImGui::EndMenu();
	}


	void EditMenu::HandleShortcuts(EditorContext& context)
	{
		// Shortcut は毎フレーム呼ぶ(呼んだフレームだけ経路が登録されるので、条件で呼び分けない)。
		// 修飾キーは完全に一致したときだけ通るので、Ctrl+Shift+Z で Ctrl+Z は通らない
		const bool undoPressed      = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, SHORTCUT_FLAGS);
		const bool redoPressed      = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, SHORTCUT_FLAGS);
		const bool shiftRedoPressed = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, SHORTCUT_FLAGS);
		if (!undoPressed && !redoPressed && !shiftRedoPressed) return;

		// 操作中の項目があるときは受けない。名前の入力中は ImGui 自身の文字の取り消しに任せ、
		// インスペクターのドラッグ中に取り消してモデルと写しが食い違うのを避ける
		if (ImGui::IsAnyItemActive()) return;
		if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
		if (!IsEnabled(context)) return;

		if (undoPressed && context.CanUndo()) {
			context.RequestUndo();
		} else if ((redoPressed || shiftRedoPressed) && context.CanRedo()) {
			context.RequestRedo();
		}
	}


	bool EditMenu::IsEnabled(const EditorContext& context)
	{
		return aq::AppHost::Get().GetRunMode() == aq::RunMode::Edit
		    && context.GetModel() != nullptr
		    && !context.HasPendingFileRequest();
	}
}
