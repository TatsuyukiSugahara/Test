#include "stdafx.h"
#include "LevelFileMenu.h"
#include "../EditorContext.h"
#include "Core/AppHost.h"
#include "Level/LevelEditModel.h"
#include <imgui/imgui.h>
#include <cstdio>
#include <string>


namespace editor
{
	namespace
	{
		/** パスが未定のときにモーダルへ入れておくパス */
		static constexpr const char* DEFAULT_LEVEL_PATH = "Assets/Levels/New.level.json";

		/** モーダルの ID(### 以降で ID を決め、題名は用途で変える) */
		static constexpr const char* PATH_POPUP_ID      = "###LevelPathPopup";
		static constexpr const char* CONFIRM_POPUP_NAME = "Unsaved Changes";

		/** 失敗・拒否の表示色 */
		static constexpr ImVec4 ERROR_TEXT_COLOR = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);


		/** 前後の空白を除く */
		std::string Trim(const std::string& text)
		{
			const size_t begin = text.find_first_not_of(" \t\r\n");
			if (begin == std::string::npos) return std::string();
			const size_t end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
		}
	}


	void LevelFileMenu::DrawMenu(EditorContext& context)
	{
		// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
		if (!ImGui::BeginMenu("File", IsEnabled(context))) return;

		if (ImGui::MenuItem("New")) {
			BeginNew(context);
		}
		if (ImGui::MenuItem("Open...")) {
			BeginOpen(context);
		}
		// 変更が無く、パスも決まっているときは保存するものが無い
		const bool canSave = context.IsDirty() || context.GetCurrentPath().empty();
		if (ImGui::MenuItem("Save", "Ctrl+S", false, canSave)) {
			BeginSave(context);
		}
		if (ImGui::MenuItem("Save As...")) {
			OpenPathDialog(context, PathDialog::SaveAs);
		}

		ImGui::EndMenu();
	}


	void LevelFileMenu::DrawStatus(const EditorContext& context)
	{
		ImGui::Separator();

		// ウィンドウのタイトルの代わりに、今のパスと変更の印を出す
		const std::string& path = context.GetCurrentPath();
		ImGui::Text("%s%s", path.empty() ? "(untitled)" : path.c_str(), context.IsDirty() ? " *" : "");

		const std::string& status = context.GetStatusMessage();
		if (!status.empty()) {
			ImGui::TextColored(ERROR_TEXT_COLOR, "%s", status.c_str());
		}
	}


	void LevelFileMenu::DrawPopups(EditorContext& context)
	{
		// Ctrl+S はどのウィンドウにフォーカスがあっても効かせる(入力中の項目が使うときは除く)
		const bool savePressed = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
		if (savePressed && IsEnabled(context)
		    && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
			BeginSave(context);
		}

		// OpenPopup と BeginPopupModal は同じ ID の階層で呼ぶ必要があるので、メニューの中ではなくここで開く
		if (openConfirmPopup_) {
			openConfirmPopup_ = false;
			ImGui::OpenPopup(CONFIRM_POPUP_NAME);
		}
		if (openPathPopup_) {
			openPathPopup_ = false;
			ImGui::OpenPopup(PATH_POPUP_ID);
		}

		DrawConfirmPopup(context);
		DrawPathPopup(context);
	}


	void LevelFileMenu::BeginNew(EditorContext& context)
	{
		if (context.IsDirty()) {
			confirmAction_    = ConfirmAction::New;
			openConfirmPopup_ = true;
			return;
		}
		context.RequestNew();
	}


	void LevelFileMenu::BeginOpen(EditorContext& context)
	{
		if (context.IsDirty()) {
			confirmAction_    = ConfirmAction::Open;
			openConfirmPopup_ = true;
			return;
		}
		OpenPathDialog(context, PathDialog::Open);
	}


	void LevelFileMenu::BeginSave(EditorContext& context)
	{
		// New の直後などパスが未定なら、保存先を決めてもらう
		if (context.GetCurrentPath().empty()) {
			OpenPathDialog(context, PathDialog::SaveAs);
			return;
		}
		// 変更が無ければファイルに触れない(更新時刻を変えない)
		if (!context.IsDirty()) return;
		context.RequestSave();
	}


	void LevelFileMenu::OpenPathDialog(EditorContext& context, const PathDialog dialog)
	{
		const std::string& currentPath = context.GetCurrentPath();
		std::snprintf(pathBuffer_, sizeof(pathBuffer_), "%s", currentPath.empty() ? DEFAULT_LEVEL_PATH : currentPath.c_str());
		pathDialog_    = dialog;
		pathError_.clear();
		openPathPopup_ = true;
	}


	bool LevelFileMenu::ConfirmPath(EditorContext& context)
	{
		const std::string path = Trim(pathBuffer_);
		if (path.empty()) {
			pathError_ = "Enter a path.";
			return false;
		}

		switch (pathDialog_) {
		case PathDialog::Open:
			context.RequestOpen(path);
			return true;

		case PathDialog::SaveAs:
		{
			const aq::level::LevelEditModel* model = context.GetModel();
			if (model == nullptr) return true;
			// 相対参照があるモデルを別のディレクトリへ保存すると、参照の指す先が変わってしまうので断る
			if (!EditorContext::CanSaveTo(*model, path)) {
				const std::string& baseDir = model->GetBaseDir();
				pathError_ = "This level has relative references (prefab / sub level paths).\n"
				             "It can only be saved in the same folder: "
				           + (baseDir.empty() ? std::string("(current directory)") : baseDir);
				return false;
			}
			context.RequestSaveAs(path);
			return true;
		}

		default:
			return true;
		}
	}


	void LevelFileMenu::DrawConfirmPopup(EditorContext& context)
	{
		if (!ImGui::BeginPopupModal(CONFIRM_POPUP_NAME, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

		ImGui::TextUnformatted("The level has unsaved changes.");
		ImGui::TextUnformatted("Discard them?");
		ImGui::Spacing();

		if (ImGui::Button("Discard")) {
			// 再生が始まっていたら何もしない(モデルは再生中に変えない)
			if (IsEnabled(context)) {
				if (confirmAction_ == ConfirmAction::New) {
					context.RequestNew();
				} else if (confirmAction_ == ConfirmAction::Open) {
					OpenPathDialog(context, PathDialog::Open);
				}
			}
			confirmAction_ = ConfirmAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			confirmAction_ = ConfirmAction::None;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}


	void LevelFileMenu::DrawPathPopup(EditorContext& context)
	{
		// 題名は用途で変え、ID は ### 以降で固定する
		const std::string title = std::string((pathDialog_ == PathDialog::SaveAs) ? "Save Level As" : "Open Level") + PATH_POPUP_ID;
		if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

		// 再生が始まったら閉じる(モデルは再生中に変えない)
		if (!IsEnabled(context)) {
			pathDialog_ = PathDialog::None;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		if (ImGui::IsWindowAppearing()) {
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(420.0f);
		const bool entered = ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::IsItemEdited()) {
			pathError_.clear();
		}

		if (!pathError_.empty()) {
			ImGui::TextColored(ERROR_TEXT_COLOR, "%s", pathError_.c_str());
		}
		ImGui::Spacing();

		const char* okLabel = (pathDialog_ == PathDialog::SaveAs) ? "Save" : "Open";
		if (ImGui::Button(okLabel) || entered) {
			if (ConfirmPath(context)) {
				pathDialog_ = PathDialog::None;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			pathDialog_ = PathDialog::None;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}


	bool LevelFileMenu::IsEnabled(const EditorContext& context)
	{
		return aq::AppHost::Get().GetRunMode() == aq::RunMode::Edit && context.GetModel() != nullptr;
	}
}
