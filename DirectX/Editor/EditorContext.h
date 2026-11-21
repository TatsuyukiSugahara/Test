#pragma once
#include "Commands/IEditorCommand.h"
#include "Level/LevelId.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace aq
{
	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	/**
	 * エディタの編集の状態。
	 * 編集モデル(Level エディタのパネルが持つもの)への参照、選択、コマンドの列、変更の有無、版番号、今のパスを持つ。
	 * パネルは const のモデルを読み、選択の読み書きと Submit / ファイル操作の要求だけを行う。
	 * モデルを書き換えるのは ApplyPending(コマンドの実行とモデルの差し替え)だけ
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


	private:
		/** 次のフレームの先頭で実行するコマンド(積んだ順) */
		std::vector<std::unique_ptr<IEditorCommand>> pendingCommands_;

		/** 次のフレームの先頭で行うファイル操作(同じフレームに複数来たら最後のもの) */
		FileRequest fileRequest_     = FileRequest::None;
		std::string fileRequestPath_;

		/** 選択中のノード ID(0 = 選択なし) */
		uint32_t selectedId_ = 0;

		/** 編集の状態 */
		bool        dirty_   = false;
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

		/** 新規の Level にする */
		void RequestNew();
		/** path の Level を開く */
		void RequestOpen(const std::string& path);
		/** 今のパスへ保存する(変更があるときだけ書く) */
		void RequestSave();
		/** path へ保存し、以後のパスにする */
		void RequestSaveAs(const std::string& path);

		/**
		 * 積んだコマンドを順に実行し、ファイル操作を行い、必要ならプレビューを置き換える。
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

		inline bool               IsDirty() const              { return dirty_; }
		inline uint32_t           GetVersion() const           { return version_; }
		inline const std::string& GetCurrentPath() const       { return currentPath_; }
		inline const std::string& GetStatusMessage() const     { return statusMessage_; }
		inline bool               HasPendingFileRequest() const { return fileRequest_ != FileRequest::None; }


	private:
		/** 積んだコマンドを実行する。1 つでも成功したら true */
		bool ApplyCommands(aq::level::LevelEditModel& model);

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


	private:
		/** 編集モデル(書き換えてよいのは ApplyPending の中だけ) */
		static aq::level::LevelEditModel* FindModel();
	};
}
