#pragma once
#include "../AssetCatalog.h"
#include <imgui/imgui.h>
#include <string>


namespace editor
{
	struct AssetFolder;
	struct AssetEntry;
	class EditorContext;




	/**
	 * コンテンツブラウザ(アセットのフォルダの木と一覧)。
	 * 左にフォルダの木、右に今のフォルダのパス・絞り込み・サブフォルダとファイルの列を出す。
	 * 種類のあるファイルはインスペクターのパスのフィールドへドラッグできる。
	 * ヒエラルキーのノードをフォルダへ落とすと Prefab を作って参照ノードに置き換える(コマンドは EditorContext へ Submit する)。
	 * Level を開く要求は持つだけで、LevelFileMenu へ渡すのは EditorLayer が行う(パネルは他のパネルを呼ばない)
	 */
	class ContentBrowserPanel
	{
	public:
		/** ウィンドウ名。ドッキング配置は editor_layout.ini にこの名前で保存される */
		static constexpr const char* WINDOW_NAME = "Content Browser";


	private:
		/** アセットの木。初めて描くときに読む */
		AssetCatalog catalog_;

		/** 選んでいるフォルダのパス(Refresh の後は FindFolder で確かめ、無ければ根に戻す) */
		std::string selectedFolderPath_;

		/** 左の木で、このパスまでのフォルダを次のフレームに開く(サブフォルダへ移った直後に使う。空なら何もしない) */
		std::string revealFolderPath_;

		/** 選んでいるファイルのパス */
		std::string selectedFilePath_;

		/** 取り出されていない Level を開く要求(パス。空なら無い) */
		std::string pendingOpenLevel_;

		/** ブラウザの上に赤で出す失敗の理由(空なら出さない)。次の成功・Refresh で消す */
		std::string errorText_;

		/** 名前の絞り込み(部分一致、大文字小文字を無視) */
		ImGuiTextFilter filter_;

		/** 次のフレームの先頭で木を読み直す */
		bool isRefreshRequested_ = false;

		/** ウィンドウを出すか */
		bool isOpen_ = true;


	public:
		/**
		 * ウィンドウを描く
		 * @param placeFloatingIfNew 配置の保存が無いとき、画面下の中央に浮かせて出すか
		 */
		void Draw(EditorContext& context, const bool placeFloatingIfNew);

		/** Level を開く要求を取り出す。要求が無ければ空文字列。取り出したら要求は空になる */
		std::string TakeOpenLevelRequest();


	public:
		inline bool IsOpen() const               { return isOpen_; }
		inline void SetOpen(const bool isOpen)   { isOpen_ = isOpen; }


	private:
		/** 木を読み直し、選んでいたフォルダが無くなっていたら根に戻す */
		void Refresh();

		/** 左の列(フォルダの木) */
		void DrawFolderTree(EditorContext& context);

		/** 木の 1 フォルダとその子を描く */
		void DrawFolderNode(EditorContext& context, const AssetFolder& folder);

		/** 右の列(パス・Refresh・絞り込み・一覧) */
		void DrawContents(EditorContext& context);

		/** 右の列の一覧(サブフォルダとファイル) */
		void DrawEntries(const AssetFolder& folder);

		/** ファイル 1 件(選択・ツールチップ・ドラッグ元・ダブルクリック・右クリック) */
		void DrawFileEntry(const AssetEntry& entry);

		/**
		 * 直前の項目(フォルダの木の項目、一覧の子ウィンドウ)をヒエラルキーのノードのドロップ先にし、
		 * 落とされたら folderPath に Prefab を作って元のノードを参照ノードに置き換える。
		 * 受けられない荷物(Edit でない・参照ノード・editId が 0・今のモデルのノードでない)には AcceptDragDropPayload を呼ばない
		 */
		void AcceptNodeDrop(EditorContext& context, const std::string& folderPath);

		/** ファイルのダブルクリック(.level.json は Edit のときだけ要求を作る。.prefab.json は Prefab エディタで開く) */
		void OnFileDoubleClicked(const AssetEntry& entry);

		/** 右クリックのメニュー(Copy Path) */
		void DrawCopyPathMenu(const std::string& path);

		/** path が folderPath のフォルダ以下(自身を含む)か */
		static bool IsInsideFolder(const std::string& path, const std::string& folderPath);
	};
}
