#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "UI/UITypes.h"
#include <string>


namespace aq
{
	namespace ui
	{
		// UI Editor と Animation 編集(Animation Editor)が共有する状態。
		// 選択・保存先・未保存フラグを 1 箇所にまとめ、
		// 複数エディタが別々の選択ハンドルや保存パスを持つ食い違いを防ぐ。
		// 選択元は UI Editor の Hierarchy だけとし、他のエディタはここを参照するだけにする。
		struct UIEditorSession
		{
			/** 選択関連 */
			UIObjectHandle selectedObject;   // 選択元は UI Editor の Hierarchy だけ
			std::string    screenName;       // 先頭画面の名前
			std::string    documentPath;     // 登録済みドキュメントパス(保存先)
			bool           dirty = false;    // 未保存の編集あり

			/** 関数ローカル static の単一インスタンスを返す */
			static UIEditorSession& Get()
			{
				static UIEditorSession instance;
				return instance;
			}

			/** 選択を解除する */
			void ClearSelection() { selectedObject = UIObjectHandle::Invalid(); }
		};

	} // namespace ui
} // namespace aq
#endif
