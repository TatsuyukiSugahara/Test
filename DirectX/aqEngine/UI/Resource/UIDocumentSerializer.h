#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include "UI/UITypes.h"

namespace aq
{
	namespace ui
	{
		class UIObject;

		// UIObject ツリーを JSON ファイルへ書き出すシリアライザー。
		// UIDocumentLoader::Load で読み込んだ構造と完全互換のフォーマットを生成する。
		//
		// テクスチャパスはランタイムでは SRV ポインタしか持たないため、
		// 呼び出し側 (UIEditorDebugPanel) が管理する texturePaths マップを渡す。
		class UIDocumentSerializer
		{
		public:
			// texturePaths: UIObjectID -> ファイルパス文字列
			// filePath が書き込めない場合は false を返す。
			static bool Save(
				const UIObject*                                   root,
				std::string_view                                  filePath,
				const std::unordered_map<UIObjectID, std::string>& texturePaths);
		};

	} // namespace ui
} // namespace aq
