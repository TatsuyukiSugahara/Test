#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include "UITypes.h"

namespace aq
{
	namespace ui
	{
		class UIObject;
		class UIScreenManager;
		class UIInputSystem;
		class UIBatchRenderer;


		// UIContext: UI サブシステム全体のシングルトンファサード。
		// UIObject の生成・破棄・解決 (UIObjectHandle → UIObject*) を担う中央レジストリ。
		class UIContext
		{
		public:
			static void       Initialize();
			static UIContext& Get();
			static void       Finalize();

			// ---- UIObject ライフタイム管理 ----

			// 名前付き UIObject を生成し、ハンドルを割り当てる。
			// 呼び出し元は返却ポインタを所有しない (内部管理)。
			UIObject* CreateObject(std::string_view name = "");

			// UIObject を破棄。子もすべて破棄される。
			// 破棄後、既存の UIObjectHandle はすべて stale になる。
			void DestroyObject(UIObject* obj);
			void DestroyObject(UIObjectHandle handle);

			// ---- 中央レジストリ (Handle 解決) ----

			// 世代不一致 / 無効 ID の場合は nullptr を返す。
			UIObject* Resolve(UIObjectHandle handle) const;

			// ---- 検索 ----

			// 名前でルート階層から再帰検索 (遅い、エディター用途向け)
			UIObject* FindByName(std::string_view name) const;

			// ---- サブシステムアクセス ----

			UIScreenManager& Screens();
			UIInputSystem&   GetInputSystem();
			UIBatchRenderer& GetBatchRenderer();

			// ---- 内部: UIObject が constructor/destructor で呼ぶ ----

			UIObjectHandle Register(UIObject* obj);
			void           Unregister(UIObjectHandle handle);

		private:
			UIContext();
			~UIContext();

			// 中央レジストリスロット
			struct Slot
			{
				UIObject* ptr        = nullptr;
				uint32_t  generation = 0u;
			};

			std::vector<Slot>        objectSlots_;
			std::vector<UIObjectID>  freeList_;

			// 所有する UIObject (ルートオブジェクトのみ; 子は UIObject が所有)
			std::vector<std::unique_ptr<UIObject>> rootObjects_;

			std::unique_ptr<UIScreenManager> screenManager_;
			std::unique_ptr<UIInputSystem>   inputSystem_;
			std::unique_ptr<UIBatchRenderer> batchRenderer_;

			static UIContext* sInstance_;
		};

	} // namespace ui
} // namespace aq
