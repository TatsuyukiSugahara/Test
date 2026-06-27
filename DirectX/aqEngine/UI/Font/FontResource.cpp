#include "aq.h"
#include "FontResource.h"

namespace aq
{
	namespace ui
	{
		bool FontLoader::Loading()
		{
			auto* fontAsset = static_cast<FontAsset*>(resource_->data_);
			if (!fontAsset) return false;

			// 1.9MB 規模の JSON パース。worker スレッドで実行されるためメインは止まらない。
			return fontAsset->LoadFromJson(requestPath_.c_str());
		}

	} // namespace ui
} // namespace aq
