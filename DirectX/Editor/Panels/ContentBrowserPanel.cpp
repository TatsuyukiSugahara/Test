#include "stdafx.h"
#include "ContentBrowserPanel.h"
#include "Core/AppHost.h"
#include "Editor/AssetDragDrop.h"
#include <string>
#include <utility>


namespace editor
{
	namespace
	{
		/** 浮かせて出すときの大きさ(メインビューポートの作業領域に対する割合) */
		static constexpr float FLOATING_WIDTH_RATIO  = 0.60f;
		static constexpr float FLOATING_HEIGHT_RATIO = 0.25f;

		/** 左の列(フォルダの木)の初期の幅 */
		static constexpr float TREE_COLUMN_WIDTH = 180.0f;

		/** 絞り込みの入力欄の幅 */
		static constexpr float FILTER_INPUT_WIDTH = 200.0f;

		/** サブフォルダの印 */
		static constexpr const char* FOLDER_TAG = "[D]";

		/** Level のファイルの末尾 */
		static constexpr const char* LEVEL_SUFFIX = ".level.json";


		/** text が suffix で終わるか(大文字小文字を無視する) */
		bool EndsWithIgnoreCase(const std::string& text, const std::string& suffix)
		{
			if (text.size() < suffix.size()) return false;
			const size_t offset = text.size() - suffix.size();
			for (size_t i = 0; i < suffix.size(); ++i) {
				char a = text[offset + i];
				char b = suffix[i];
				if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
				if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
				if (a != b) return false;
			}
			return true;
		}
	}


	void ContentBrowserPanel::Draw(const bool placeFloatingIfNew)
	{
		if (!isOpen_) return;

		// 初めて描くときに読む(その後は Refresh ボタンだけ)
		if (!catalog_.IsLoaded()) {
			Refresh();
		}

		// 配置の保存が無いときだけ効く(ImGuiCond_FirstUseEver)。ドッキングすれば以後は ini に残る
		if (placeFloatingIfNew) {
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y),
			                        ImGuiCond_FirstUseEver, ImVec2(0.5f, 1.0f));
			ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * FLOATING_WIDTH_RATIO, viewport->WorkSize.y * FLOATING_HEIGHT_RATIO),
			                         ImGuiCond_FirstUseEver);
		}

		if (ImGui::Begin(WINDOW_NAME, &isOpen_)) {
			if (ImGui::BeginTable("ContentBrowserTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, TREE_COLUMN_WIDTH);
				ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				DrawFolderTree();

				ImGui::TableSetColumnIndex(1);
				DrawContents();

				ImGui::EndTable();
			}
		}
		ImGui::End();

		// 開く指示は 1 フレームだけ有効
		revealFolderPath_.clear();
	}


	std::string ContentBrowserPanel::TakeOpenLevelRequest()
	{
		return std::exchange(pendingOpenLevel_, std::string());
	}


	void ContentBrowserPanel::Refresh()
	{
		catalog_.Refresh();

		// 選んでいたフォルダが無くなっていたら根に戻す
		if (selectedFolderPath_.empty() || catalog_.FindFolder(selectedFolderPath_) == nullptr) {
			selectedFolderPath_ = catalog_.GetTree().path;
		}
	}


	void ContentBrowserPanel::DrawFolderTree()
	{
		ImGui::BeginChild("FolderTree");
		DrawFolderNode(catalog_.GetTree());
		ImGui::EndChild();
	}


	void ContentBrowserPanel::DrawFolderNode(const AssetFolder& folder)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (folder.folders.empty()) {
			flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (folder.path == selectedFolderPath_) {
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		// サブフォルダへ移った直後は、そこまでの親を開いて見えるようにする。根は初期状態でも開く
		if (!revealFolderPath_.empty() && IsInsideFolder(revealFolderPath_, folder.path)) {
			ImGui::SetNextItemOpen(true);
		} else if (folder.path == catalog_.GetTree().path) {
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		const bool isOpenNode = ImGui::TreeNodeEx(folder.path.c_str(), flags, "%s", folder.name.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			selectedFolderPath_ = folder.path;
		}
		if (isOpenNode) {
			for (const AssetFolder& child : folder.folders) {
				DrawFolderNode(child);
			}
			ImGui::TreePop();
		}
	}


	void ContentBrowserPanel::DrawContents()
	{
		// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
		ImGui::TextUnformatted(selectedFolderPath_.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			Refresh();
		}
		ImGui::SameLine();
		filter_.Draw("Filter", FILTER_INPUT_WIDTH);
		ImGui::Separator();

		ImGui::BeginChild("Entries");
		const AssetFolder* folder = catalog_.FindFolder(selectedFolderPath_);
		if (folder != nullptr) {
			DrawEntries(*folder);
		}
		ImGui::EndChild();
	}


	void ContentBrowserPanel::DrawEntries(const AssetFolder& folder)
	{
		// サブフォルダ
		for (const AssetFolder& child : folder.folders) {
			if (!filter_.PassFilter(child.name.c_str())) continue;

			ImGui::PushID(child.path.c_str());
			const std::string label = std::string(FOLDER_TAG) + " " + child.name;
			ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				selectedFolderPath_ = child.path;
				revealFolderPath_   = child.path;
			}
			DrawCopyPathMenu(child.path);
			ImGui::PopID();
		}

		// ファイル
		for (const AssetEntry& entry : folder.files) {
			if (!filter_.PassFilter(entry.name.c_str())) continue;
			DrawFileEntry(entry);
		}
	}


	void ContentBrowserPanel::DrawFileEntry(const AssetEntry& entry)
	{
		ImGui::PushID(entry.path.c_str());

		// 種類の印は種類の無いファイルでは空なので、そのときは名前だけにする
		const char* tag = AssetCatalog::GetKindTag(entry.kind);
		const std::string label = (tag != nullptr && tag[0] != '\0') ? (std::string(tag) + " " + entry.name) : entry.name;

		const bool isSelected = (entry.path == selectedFilePath_);
		if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
			selectedFilePath_ = entry.path;
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
			ImGui::SetTooltip("%s", entry.path.c_str());
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			OnFileDoubleClicked(entry);
		}

		// 種類のあるファイルだけドラッグ元にする。荷物を置けなかったら(パスが長すぎるなど)何も出さない
		if (entry.kind != aq::ecs::AssetKind::None && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
			if (aq::editor::SetAssetDragPayload(entry.kind, entry.path)) {
				ImGui::TextUnformatted(entry.name.c_str());
			}
			ImGui::EndDragDropSource();
		}

		DrawCopyPathMenu(entry.path);
		ImGui::PopID();
	}


	void ContentBrowserPanel::OnFileDoubleClicked(const AssetEntry& entry)
	{
		// Level を開けるのは Edit のときだけ(再生中はモデルを変えない)
		if (entry.kind == aq::ecs::AssetKind::Level && EndsWithIgnoreCase(entry.name, LEVEL_SUFFIX)) {
			if (aq::AppHost::Get().GetRunMode() == aq::RunMode::Edit) {
				pendingOpenLevel_ = entry.path;
			}
		}
		// .prefab.json を Prefab エディタで開くのは E9b
	}


	void ContentBrowserPanel::DrawCopyPathMenu(const std::string& path)
	{
		if (!ImGui::BeginPopupContextItem("ContentBrowserContext")) return;

		if (ImGui::MenuItem("Copy Path")) {
			ImGui::SetClipboardText(path.c_str());
		}
		ImGui::EndPopup();
	}


	bool ContentBrowserPanel::IsInsideFolder(const std::string& path, const std::string& folderPath)
	{
		if (path == folderPath) return true;
		return path.size() > folderPath.size()
		    && path.compare(0, folderPath.size(), folderPath) == 0
		    && path[folderPath.size()] == '/';
	}
}
