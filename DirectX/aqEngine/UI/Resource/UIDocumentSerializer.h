#pragma once
#include <string>
#include <string_view>
#include "UI/UITypes.h"

namespace aq
{
	namespace ui
	{
		class UIObject;

		// UIObject ツリーを JSON ファイルへ書き出すシリアライザー。
		// UIDocumentLoader::Load で読み込んだ構造と完全互換のフォーマットを生成する。
		//
		// テクスチャパスは Image / NineSlice / CircleGauge 各コンポーネントの
		// texturePath が正本であり、それをそのまま書き出す。
		class UIDocumentSerializer
		{
		public:
			// filePath が書き込めない場合は false を返す。
			static bool Save(const UIObject* root, std::string_view filePath);
		};

	} // namespace ui
} // namespace aq
