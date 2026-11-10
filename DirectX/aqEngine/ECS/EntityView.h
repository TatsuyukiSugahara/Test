#pragma once
#include "Chunk.h"
#include <tuple>
#include <utility>
#include <vector>


namespace aq
{
	namespace ecs
	{
		class EntityManager;  // forward declaration


		// 対象 Component セットを持つ Chunk の軽量クエリビュー。
		// 対象 Chunk のインデックスは ForEach が iterationMutex_ のリードロックを取った後に求める
		// (GetView の時点では chunkList_ を読まない。ロック外で読むと CreateEntity 等の
		// chunkList_ 再確保と競合するため)。Chunk* ではなくインデックスで走査する。
		// ForEach 中に AddComponent / Destroy を呼んでも Chunk 構造は変わらない
		// （FlushCommands で反映）。
		template <typename... Components>
		class EntityView
		{
		public:
			explicit EntityView(EntityManager* manager)
				: manager_(manager)
			{
			}

			// ForEach の定義は EntityManager.h 末尾に置く（EntityManager の完全定義が必要）
			template <typename Func>
			void ForEach(Func&& func) const;

		private:
			EntityManager* manager_;
		};
	}
}
