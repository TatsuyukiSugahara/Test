#pragma once
#include <memory>
#include <string_view>
#include "UI/UITypes.h"

namespace aq
{
	namespace ui
	{
		class UIObject;
		class UIScreenManager;


		// UIScreen: 1画面分の UIObject ツリーとライフサイクルを管理する基底クラス。
		// ゲーム側で派生し、OnCreate()/OnEnter() 等をオーバーライドして使う。
		class UIScreen
		{
		public:
			virtual ~UIScreen() = default;

			// ---- ライフサイクル ----
			// OnCreate : Push 時に初回のみ呼ばれる。callback バインドはここで行う。
			// OnEnter  : 表示開始のたびに呼ばれる。入場アニメ開始・フォーカス設定はここ。
			// OnPause  : 上に別画面が積まれたとき。
			// OnResume : 上の画面が閉じて戻ったとき。
			// OnExit   : Pop/Replace で閉じ始めたとき。
			// OnDestroy: 完全に破棄されたとき。
			// OnBack   : 戻る操作。true を返すと処理済みとみなし Pop しない。
			virtual void OnCreate()  {}
			virtual void OnEnter()   {}
			virtual void OnPause()   {}
			virtual void OnResume()  {}
			virtual void OnExit()    {}
			virtual void OnDestroy() {}
			virtual bool OnBack()    { return false; }

			// ---- 入力・描画制御フラグ ----
			bool blocksRaycast = true;  // false → マウス HitTest を背面画面に通す
			bool blocksInput   = true;  // false → キーボード/Pad 入力を背面画面に通す
			bool renderBelow   = false; // true  → スタック中で背面レイヤーに描画

			// ---- アクセサ ----
			UIObject*        GetRoot()   const { return root_; }
			std::string_view GetName()   const { return name_; }

			// ルート以下から名前で UIObject を検索 (再帰)
			UIObjectHandle FindHandle(std::string_view name) const;

		protected:
			// 派生クラスから子 UIObject のハンドルを解決するショートカット
			UIObject* Resolve(UIObjectHandle handle) const;

		private:
			friend class UIScreenManager;

			std::string name_;
			UIObject*   root_ = nullptr; // UIContext が所有; UIScreen は非所有参照
		};

	} // namespace ui
} // namespace aq
