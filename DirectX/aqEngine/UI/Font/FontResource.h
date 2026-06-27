#pragma once
#include <memory>
#include "Resource/Resource.h"
#include "UI/Font/FontAsset.h"

namespace aq
{
	namespace ui
	{
		/**
		 * フォントアトラス (atlas.json + atlas.png) を非同期ロードするリソース。
		 *
		 * 重い JSON パース (グリフ DB 構築) を worker スレッドで行い、
		 * メインスレッドのストール (起動時のハング) を防ぐ。
		 * data_ はパース結果を保持する FontAsset。
		 */
		class FontResource : public res::ResourceBase
		{
			engineResource(aq::ui::FontResource);

		public:
			FontResource() : res::ResourceBase() { data_ = new FontAsset(); }

			virtual ~FontResource()
			{
				delete static_cast<FontAsset*>(data_);
				data_ = nullptr;
			}

			FontAsset* GetFontAsset() const { return static_cast<FontAsset*>(data_); }
		};
		using RefFontResource = std::shared_ptr<FontResource>;


		/**
		 * FontResource 用ローダー。
		 * Loading() が worker スレッド上で atlas.json を解析する。
		 * atlas.png は LoadFromJson 内で Load<GPUResource>() により別途非同期ロードされる。
		 */
		class FontLoader : public res::ResourceLoaderBase
		{
		public:
			FontLoader() = default;
			~FontLoader() = default;

			virtual res::ResourceBase* Create() override { return new FontResource(); }

		private:
			bool Loading() override;
		};

	} // namespace ui
} // namespace aq
