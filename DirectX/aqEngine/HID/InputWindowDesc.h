#pragma once
#include "Graphics/GraphicsTypes.h"   // NativeWindowHandle


namespace aq
{
	namespace hid
	{
		/**
		 * キーボード / マウスのバックエンドへ渡すウィンドウ情報。
		 *
		 * 座標変換の基準(描画先)と、DirectInput の協調レベル設定先を分けて持つ。
		 * エディタへ埋め込んだ子ウィンドウは DirectInput の協調ウィンドウとして受け付けられないため、
		 * 埋め込み時は協調用に別の非表示トップレベルウィンドウを渡す。
		 * 埋め込みでないときは、両方ともメインウィンドウで background = false(従来と同じ動作)。
		 */
		struct InputWindowDesc
		{
			aq::graphics::NativeWindowHandle clientWindow;       // 描画先。座標変換の基準
			aq::graphics::NativeWindowHandle cooperativeWindow;  // DirectInput の協調レベル設定先
			bool                             background = false; // true なら DISCL_BACKGROUND
		};
	}
}
