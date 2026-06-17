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
		// GetView<T...>() 呼び出し時点の chunkIndices スナップショットを保持する。
		// Chunk* ではなくインデックスを保持することで、CreateEntity 等による
		// chunkList_ の再確保後も dangling pointer にならない。
		// ForEach 中に AddComponent / Destroy を呼んでも Chunk 構造は変わらない
		// （FlushCommands で反映）。
		template <typename... Components>
		class EntityView
		{
		public:
			EntityView(EntityManager* manager, std::vector<uint32_t> indices)
				: manager_(manager)
				, chunkIndices_(std::move(indices))
			{
			}

			// ForEach の定義は EntityManager.h 末尾に置く（EntityManager の完全定義が必要）
			template <typename Func>
			void ForEach(Func&& func) const;

		private:
			EntityManager* manager_;
			std::vector<uint32_t> chunkIndices_;
		};
	}
}
