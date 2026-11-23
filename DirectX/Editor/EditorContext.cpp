#include "stdafx.h"
#include "EditorContext.h"
#include "PlayModeController.h"
#include "Core/AppHost.h"
#include "Level/LevelEditModel.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include <algorithm>
#include <string>
#include <vector>


namespace editor
{
	namespace
	{
		/** 親子の木をたどる深さの上限(循環していても止まるように) */
		static constexpr int MAX_TREE_DEPTH = 64;

		/** ログ 1 行に載せる木の文字数(StartupLog の 1 行は 512 バイトで切れるので分けて出す) */
		static constexpr size_t TREE_LOG_CHUNK_SIZE = 400;


		/** 文字列を並べ替えて区切り文字で連結する */
		std::string JoinSorted(std::vector<std::string>& items, const char* separator)
		{
			std::sort(items.begin(), items.end());
			std::string out;
			for (size_t i = 0; i < items.size(); ++i) {
				if (i > 0) out += separator;
				out += items[i];
			}
			return out;
		}


		/**
		 * エンティティとその子孫の名前の入れ子。"名前(子,子(孫))" の形。
		 * 兄弟は並べ替える(生成の順に左右されずに、操作の前後で比べられるように)
		 */
		std::string BuildNameTree(aq::ecs::EntityContext& context, const aq::ecs::EntityHandle handle, const int depth)
		{
			std::string name = "?";
			if (const aq::ecs::EntityDebugTag* tag = context.GetComponent<aq::ecs::EntityDebugTag>(handle)) {
				name = (tag->displayName[0] != '\0') ? std::string(tag->displayName) : std::string("(unnamed)");
			}
			if (depth >= MAX_TREE_DEPTH) return name + "(...)";

			std::vector<std::string> children;
			for (const aq::ecs::EntityHandle& child : context.GetChildren(handle)) {
				children.push_back(BuildNameTree(context, child, depth + 1));
			}
			if (children.empty()) return name;
			return name + "(" + JoinSorted(children, ",") + ")";
		}


		/** 履歴のログ 1 行。"[editor] <動作>: <名前> (undo=N redo=M)" の形(自動操作の確認用) */
		void LogHistory(const char* action, const char* commandName, const CommandHistory& history)
		{
			const std::string line = std::string("[editor] ") + action + ": " + commandName
			                       + " (undo=" + std::to_string(history.GetUndoCount())
			                       + " redo=" + std::to_string(history.GetRedoCount()) + ")";
			aq::StartupLog(line.c_str());
		}
	}


	void EditorContext::Submit(std::unique_ptr<IEditorCommand> command)
	{
		if (command == nullptr) return;
		// 再生中は編集モデルを変えない。モードが変わるのはフレームの先頭だけなので、Edit で積んだものは Edit のうちに実行される
		if (aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) return;
		pendingCommands_.push_back(std::move(command));
	}


	void EditorContext::RequestUndo()
	{
		// Submit と同じく、再生中の要求は捨てる(再生中は編集モデルを変えない)
		if (aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) return;
		historyRequest_ = HistoryRequest::Undo;
	}


	void EditorContext::RequestRedo()
	{
		if (aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) return;
		historyRequest_ = HistoryRequest::Redo;
	}


	void EditorContext::RequestNew()
	{
		fileRequest_ = FileRequest::New;
		fileRequestPath_.clear();
	}


	void EditorContext::RequestOpen(const std::string& path)
	{
		fileRequest_     = FileRequest::Open;
		fileRequestPath_ = path;
	}


	void EditorContext::RequestSave()
	{
		fileRequest_ = FileRequest::Save;
		fileRequestPath_.clear();
	}


	void EditorContext::RequestSaveAs(const std::string& path)
	{
		fileRequest_     = FileRequest::SaveAs;
		fileRequestPath_ = path;
	}


	void EditorContext::ApplyPending()
	{
		aq::level::LevelEditModel* model = FindModel();

		// 編集モデルが無い、または再生中は何もしない(積まれたものは捨てる)
		if (model == nullptr || aq::AppHost::Get().GetRunMode() != aq::RunMode::Edit) {
			pendingCommands_.clear();
			historyRequest_ = HistoryRequest::None;
			fileRequest_    = FileRequest::None;
			fileRequestPath_.clear();
			return;
		}

		// 起動時は空の新規 Level のプレビューを 1 回作る
		bool needPreview = false;
		if (!initialPreviewDone_) {
			initialPreviewDone_ = true;
			needPreview = true;
		}

		// コマンド → 取り消し・やり直し → ファイル操作の順
		// (同じフレームに積まれた編集は履歴に入れてから取り消しの対象にし、開き直しの前に反映してから捨てる)
		if (ApplyCommands(*model)) {
			++version_;
			needPreview = true;
		}
		if (ApplyHistoryRequest(*model)) {
			++version_;
			needPreview = true;
		}
		if (ApplyFileRequest(*model)) {
			needPreview = true;
		}

		// 削除などで消えたノードの選択を外す。自己テストなど外からモデルが変わったときもここで外れる
		if (selectedId_ != 0 && model->FindNode(selectedId_) == nullptr) {
			selectedId_ = 0;
		}

		// プレビューの置き換えはここで直接行う(完了待ちを 1 フレームに 1 回にまとめる。
		// Level エディタの DebugRender はゲーム用のデバッグ UI を隠すと呼ばれないので任せない)
		if (needPreview) {
			model->RequestPreview();
			model->ProcessPreviewRequest();
		}

		LogPreviewIfSpawned(*model);
	}


	bool EditorContext::ApplyCommands(aq::level::LevelEditModel& model)
	{
		bool anySucceeded = false;
		for (std::unique_ptr<IEditorCommand>& command : pendingCommands_) {
			if (command->Execute(model)) {
				anySucceeded = true;
				// 名前はコマンドの持ち物なので、履歴へ移す前に控える
				const std::string name = command->GetName();
				history_.Push(std::move(command));
				LogHistory("command", name.c_str(), history_);
			} else {
				// 失敗したコマンドはモデルを変えていないので、履歴に入れずに捨てる
				const std::string message = std::string("[editor] command failed: ") + command->GetName()
				                          + " (rejected by the edit model: target not found or invalid destination)";
				aq::StartupLog(message.c_str());
			}
		}
		pendingCommands_.clear();
		return anySucceeded;
	}


	bool EditorContext::ApplyHistoryRequest(aq::level::LevelEditModel& model)
	{
		const HistoryRequest request = historyRequest_;
		historyRequest_ = HistoryRequest::None;

		if (request == HistoryRequest::None) return false;
		const bool isUndo = (request == HistoryRequest::Undo);

		// 履歴が無ければ何もしない(メニューは無効にしているが、ショートカットと同じフレームに履歴が変わることがある)
		const char* nameInHistory = isUndo ? history_.GetUndoName() : history_.GetRedoName();
		if (nameInHistory == nullptr) return false;
		// 失敗すると履歴ごと捨てられるので、名前は先に控える
		const std::string name = nameInHistory;

		const bool succeeded = isUndo ? history_.Undo(model) : history_.Redo(model);
		if (!succeeded) {
			// モデルとコマンドが食い違った(通常は起きない)。履歴は空になり、保存の印も一致しなくなる(変更ありのまま)
			const std::string message = std::string("[editor] ") + (isUndo ? "undo" : "redo") + " failed: " + name
			                          + " (the edit model did not match the command; history cleared)";
			aq::StartupLog(message.c_str());
			return false;
		}

		LogHistory(isUndo ? "undo" : "redo", name.c_str(), history_);
		return true;
	}


	bool EditorContext::ApplyFileRequest(aq::level::LevelEditModel& model)
	{
		const FileRequest request = fileRequest_;
		const std::string path    = fileRequestPath_;
		fileRequest_ = FileRequest::None;
		fileRequestPath_.clear();

		switch (request) {
		case FileRequest::New:
			model.NewLevel();
			currentPath_.clear();
			// モデル全体を入れ替えたので、前のモデルへの取り消しは使えない
			history_.MarkCleanEmpty();
			selectedId_ = 0;
			++version_;
			statusMessage_.clear();
			aq::StartupLog("[editor] level new");
			return true;

		case FileRequest::Open:
			if (!model.LoadFile(path)) {
				statusMessage_ = "Open failed: " + path;
				aq::StartupLog(("[editor] level open failed: " + path).c_str());
				return false;
			}
			currentPath_ = path;
			history_.MarkCleanEmpty();
			selectedId_ = 0;
			++version_;
			statusMessage_.clear();
			aq::StartupLog(("[editor] level open: " + path).c_str());
			return true;

		case FileRequest::Save:
			// 変更が無ければファイルに触れない(更新時刻を変えない)
			if (!IsDirty() || currentPath_.empty()) return false;
			if (!model.SaveFile(currentPath_)) {
				statusMessage_ = "Save failed: " + currentPath_;
				aq::StartupLog(("[editor] level save failed: " + currentPath_).c_str());
				return false;
			}
			// モデルは入れ替えないので履歴は残す(保存の後も取り消せる)
			history_.MarkSaved();
			selectedId_ = 0;
			++version_;
			statusMessage_.clear();
			aq::StartupLog(("[editor] level save: " + currentPath_).c_str());
			return false;

		case FileRequest::SaveAs:
		{
			// 要求の後にモデルが変わっていることがあるので、保存の時点でもう一度確かめる
			if (!CanSaveTo(model, path)) {
				statusMessage_ = "Save As refused: relative references require the same folder";
				aq::StartupLog(("[editor] level save as refused (relative references): " + path).c_str());
				return false;
			}
			const std::string oldBaseDir = model.GetBaseDir();
			if (!model.SaveFile(path)) {
				statusMessage_ = "Save failed: " + path;
				aq::StartupLog(("[editor] level save as failed: " + path).c_str());
				return false;
			}
			currentPath_ = path;
			const bool baseDirChanged = model.GetBaseDir() != oldBaseDir;
			if (baseDirChanged) {
				// 履歴の中には相対参照を持つノード(削除したサブツリー、変更前の Prefab の参照)が残りうる。
				// 保存の後に取り消すと新しい基準で解決されて別のアセットを指すので、履歴を空にする
				history_.MarkCleanEmpty();
			} else {
				history_.MarkSaved();
			}
			selectedId_ = 0;
			++version_;
			statusMessage_.clear();
			aq::StartupLog(("[editor] level save as: " + path).c_str());
			// 基準ディレクトリが変わったら、プレビューの基準もモデルに揃える
			return baseDirChanged;
		}

		default:
			return false;
		}
	}


	void EditorContext::LogPreviewIfSpawned(const aq::level::LevelEditModel& model)
	{
		const aq::level::LevelId previewId = model.GetPreviewLevelId();
		if (!previewId.IsValid() || previewId == loggedPreviewId_) return;
		if (!aq::level::LevelManager::Get().IsSpawned(previewId)) return;
		loggedPreviewId_ = previewId;

		// Level ごとのエンティティを引く口が無いので、Edit のワールド全体を出す。
		// Editor は空のワールドで Edit を始めるので、自己テストの準備をしていなければワールド = プレビュー
		aq::ecs::EntityContext& context = aq::ecs::EntityContext::Get();
		std::vector<aq::ecs::EntityHandle> handles;
		context.CollectAllHandles(handles);

		size_t entityCount = 0;
		std::vector<std::string> roots;
		for (const aq::ecs::EntityHandle& handle : handles) {
			if (!context.IsValid(handle)) continue;
			++entityCount;
			if (context.IsValid(context.GetParent(handle))) continue;
			roots.push_back(BuildNameTree(context, handle, 0));
		}
		const std::string tree = JoinSorted(roots, ",");

		const std::string header = "[editor] preview spawned: version=" + std::to_string(version_)
		                         + " entities=" + std::to_string(entityCount)
		                         + " tree-length=" + std::to_string(tree.size());
		aq::StartupLog(header.c_str());

		for (size_t offset = 0, part = 0; offset < tree.size(); offset += TREE_LOG_CHUNK_SIZE, ++part) {
			const std::string line = "[editor] preview tree[" + std::to_string(part) + "]: "
			                       + tree.substr(offset, TREE_LOG_CHUNK_SIZE);
			aq::StartupLog(line.c_str());
		}
	}


	const aq::level::LevelEditModel* EditorContext::GetModel() const
	{
		return FindModel();
	}


	bool EditorContext::CanSaveTo(const aq::level::LevelEditModel& model, const std::string& path)
	{
		if (!model.HasRelativeReferences()) return true;
		// 新規(基準ディレクトリが空 = カレント基準)のモデルも同じ規則で比べる
		return DirectoryOf(path) == model.GetBaseDir();
	}


	std::string EditorContext::DirectoryOf(const std::string& path)
	{
		const std::string normalized = aq::level::LevelRegistry::Normalize(path);
		const size_t pos = normalized.find_last_of('/');
		return (pos == std::string::npos) ? std::string() : normalized.substr(0, pos);
	}


	aq::level::LevelEditModel* EditorContext::FindModel()
	{
		return PlayModeController::FindLevelEditModel();
	}
}
