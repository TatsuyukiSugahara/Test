#pragma once
#include <cstdint>
#include <string>


namespace editor
{
	class EditorContext;




	/**
	 * Level のファイル操作(メインメニューバーの File メニュー)。
	 * New / Open... / Save / Save As... と Ctrl+S。パスはモーダルで入れる。
	 * 操作は EditorContext へ要求として積むだけで、モデルの差し替えは次のフレームの先頭で行われる
	 */
	class LevelFileMenu
	{
	private:
		/** パスを入れるモーダルの用途 */
		enum class PathDialog : uint8_t
		{
			None,
			Open,
			SaveAs,
		};

		/** 未保存の確認の後に行う操作 */
		enum class ConfirmAction : uint8_t
		{
			None,
			New,
			Open,
		};


	private:
		/** パスのモーダル */
		PathDialog  pathDialog_      = PathDialog::None;
		bool        openPathPopup_   = false;     // 次の DrawPopups で開く
		char        pathBuffer_[260] = {};
		std::string pathError_;                   // モーダルに出す理由(保存を断ったときなど)

		/** 未保存の確認のモーダル */
		ConfirmAction confirmAction_    = ConfirmAction::None;
		bool          openConfirmPopup_ = false;  // 次の DrawPopups で開く
		std::string   confirmOpenPath_;           // 確認の後に開くパス(コンテンツブラウザから開くとき。空ならパスのモーダルを出す)


	public:
		/** メインメニューバーの中で File メニューを描く(BeginMainMenuBar の後に呼ぶ) */
		void DrawMenu(EditorContext& context);

		/** メインメニューバーの中に今のパスと変更の印(*)を出す */
		void DrawStatus(const EditorContext& context);

		/** モーダルと Ctrl+S。メインメニューバーの外(EndMainMenuBar の後)で毎フレーム呼ぶ */
		void DrawPopups(EditorContext& context);

		/** パスを決めて開く(コンテンツブラウザから。未保存なら確認してから、パスのモーダルを出さずに開く) */
		void BeginOpenPath(EditorContext& context, const std::string& path);


	private:
		/** 新規(未保存なら確認してから) */
		void BeginNew(EditorContext& context);
		/** 開く(未保存なら確認してから、パスのモーダルを開く) */
		void BeginOpen(EditorContext& context);
		/** 保存(パスが未定なら Save As のモーダルを開く。変更が無ければ何もしない) */
		void BeginSave(EditorContext& context);
		/** パスのモーダルを開く */
		void OpenPathDialog(EditorContext& context, const PathDialog dialog);

		/** パスのモーダルの OK。受け付けたら true(モーダルを閉じる) */
		bool ConfirmPath(EditorContext& context);

		void DrawConfirmPopup(EditorContext& context);
		void DrawPathPopup(EditorContext& context);


	private:
		/** ファイル操作を受け付けるか(Edit のときだけ。編集モデルが無ければ受け付けない) */
		static bool IsEnabled(const EditorContext& context);
	};
}
