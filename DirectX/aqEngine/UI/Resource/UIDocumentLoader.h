#pragma once
#include <string_view>

namespace aq
{
	namespace ui
	{
		class UIObject;
		class UIContext;

		// JSON ファイルから UIObject ツリーを生成するローダー。
		// "ref" フィールドで別ファイルのプレハブを参照でき、
		// "overrides" で任意フィールドを上書きできる。
		class UIDocumentLoader
		{
		public:
			// docPath の JSON を解析し、ルートの UIObject を返す。
			// docPath が空または解析失敗時は名前のみのノードを生成して返す。
			// screenName は docPath が空 / ルートに "name" がない場合のフォールバック名。
			static UIObject* Load(
				std::string_view screenName,
				std::string_view docPath,
				UIContext&       ctx);
		};

	} // namespace ui
} // namespace aq
