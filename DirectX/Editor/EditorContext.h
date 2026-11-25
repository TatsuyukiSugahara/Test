#pragma once
#include "CommandHistory.h"
#include "Commands/IEditorCommand.h"
#include "Level/LevelId.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace aq
{
	namespace ecs
	{
		struct PrefabEditNode;
	}

	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	/**
	 * エディタの編集の状態。
	 * 編集モデル(Level エディタのパネルが持つもの)への参照、選択、コマンドの列と履歴、版番号、今のパスを持つ。
	 * パネルは const のモデルを読み、選択の読み書きと Submit / Undo・Redo / ファイル操作の要求だけを行う。
	 * モデルを書き換えるのは ApplyPending(コマンドの実行、取り消しとやり直し、モデルの差し替え)だけ
	 */
	class EditorContext
	{
	private:
		/** モデル全体を入れ替える操作(コマンドにしない。取り消しの対象外) */
		enum class FileRequest : uint8_t
		{
			None,
			New,
			Open,
			Save,
			SaveAs,
		};

		/** 取り消しとやり直しの要求 */
		enum class HistoryRequest : uint8_t
		{
			None,
			Undo,
			Redo,
		};


	private:
		/** 次のフレームの先頭で実行するコマンド(積んだ順) */
		std::vector<std::unique_ptr<IEditorCommand>> pendingCommands_;

		/** 次のフレームの先頭で行う取り消し・やり直し(同じフレームに複数来たら最後のもの) */
		HistoryRequest historyRequest_ = HistoryRequest::None;

		/** 次のフレームの先頭で行うファイル操作(同じフレームに複数来たら最後のもの) */
		FileRequest fileRequest_     = FileRequest::None;
		std::string fileRequestPath_;

		/** 選択中のノード ID(0 = 選択なし) */
		uint32_t selectedId_ = 0;

		/** 実行したコマンドの履歴と保存の印(変更の有無はここから求める) */
		CommandHistory history_;

		/** 編集の状態 */
		uint32_t    version_ = 0;        // モデルが変わるたびに進める
		std::string currentPath_;        // 開いている / 保存したファイル(新規では空)

		/** ファイル操作の結果(メニューバーに出す。成功したら空) */
		std::string statusMessage_;

		/** 起動時のプレビューを作ったか */
		bool initialPreviewDone_ = false;

		/** 生成済みをログに出したプレビュー(置き換わったら次に生成されたフレームでまた出す) */
		aq::level::LevelId loggedPreviewId_;


	public:
		/**
		 * コマンドを積む。Edit のときだけ受け付け、それ以外では捨てる(再生中は編集モデルを変えない)。
		 * 実行は次のフレームの先頭の ApplyPending
		 */
		void Submit(std::unique_ptr<IEditorCommand> command);

		/**
		 * 直前のコマンドを取り消す / 取り消したコマンドをやり直す。Edit のときだけ受け付け、それ以外では捨てる。
		 * 実行は次のフレームの先頭の ApplyPending(そのフレームに積まれたコマンドの後、ファイル操作の前)
		 */
		void RequestUndo();
		void RequestRedo();

		/** 新規の Level にする */
		void RequestNew();
		/** path の Level を開く */
		void RequestOpen(const std::string& path);
		/** 今のパスへ保存する(変更があるときだけ書く) */
		void RequestSave();
		/** path へ保存し、以後のパスにする */
		void RequestSaveAs(const std::string& path);

		/**
		 * 積んだコマンドを順に実行し、取り消し・やり直しとファイル操作を行い、必要ならプレビューを置き換える。
		 * EditorLayer::OnBeginFrame で、PlayModeController::ApplyPendingRequest より前に毎フレーム 1 回呼ぶ
		 * (FlushRender の後・ECS の反復の外。ゲーム用の ImGui コンテキストを current にした区間)
		 */
		void ApplyPending();


	public:
		/** 編集モデル(Level エディタのパネルが無ければ nullptr)。パネルはこれを読むだけ */
		const aq::level::LevelEditModel* GetModel() const;

		/** 選択中のノード ID(0 = 選択なし)。モデルが変わって消えたノードは ApplyPending が外す */
		inline uint32_t GetSelectedId() const               { return selectedId_; }
		inline void     SetSelectedId(const uint32_t nodeId) { selectedId_ = nodeId; }
		inline void     ClearSelection()                     { selectedId_ = 0; }

		/** 保存した時点から変わっているか(履歴の今の状態が保存の印と違う) */
		inline bool               IsDirty() const              { return !history_.IsAtSavedState(); }
		inline uint32_t           GetVersion() const           { return version_; }
		inline const std::string& GetCurrentPath() const       { return currentPath_; }
		inline const std::string& GetStatusMessage() const     { return statusMessage_; }
		inline bool               HasPendingFileRequest() const { return fileRequest_ != FileRequest::None; }

		/** 取り消し・やり直しができるか(Edit メニュー用) */
		inline bool CanUndo() const { return history_.CanUndo(); }
		inline bool CanRedo() const { return history_.CanRedo(); }

		/** 次に取り消す / やり直すコマンドの名前(無ければ nullptr) */
		inline const char* GetUndoName() const { return history_.GetUndoName(); }
		inline const char* GetRedoName() const { return history_.GetRedoName(); }


	private:
		/** 積んだコマンドを実行し、成功したものを履歴へ積む。1 つでも成功したら true */
		bool ApplyCommands(aq::level::LevelEditModel& model);

		/** 取り消し・やり直しの要求を行う。モデルが変わったら true */
		bool ApplyHistoryRequest(aq::level::LevelEditModel& model);

		/** ファイル操作を行う。プレビューを置き換えるべきなら true */
		bool ApplyFileRequest(aq::level::LevelEditModel& model);

		/** プレビューが置き換わって生成されたら、エンティティ数と親子の木をログへ 1 回出す */
		void LogPreviewIfSpawned(const aq::level::LevelEditModel& model);


	public:
		/**
		 * 別のディレクトリへの保存を許すか。相対パスの参照(Prefab の参照・ファイル参照の subLevel)があると、
		 * 保存先のディレクトリが変わると指す先が変わってしまうので、同じディレクトリにしか保存できない
		 */
		static bool CanSaveTo(const aq::level::LevelEditModel& model, const std::string& path);

		/** path のディレクトリ(編集モデルの基準ディレクトリと同じ求め方) */
		static std::string DirectoryOf(const std::string& path);

		/** node が model のノード(トップレベルとインラインの subLevel の entities の木)か。ドラッグの荷物のポインタを使う前に、モデルが変わって無効になっていないかを確かめる(比べるだけで読まない) */
		static bool ContainsNode(const aq::level::LevelEditModel& model, const aq::ecs::PrefabEditNode* node);


	private:
		/** 編集モデル(書き換えてよいのは ApplyPending の中だけ) */
		static aq::level::LevelEditModel* FindModel();
	};
}
