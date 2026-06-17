#pragma once
#include <unordered_map>
#include <vector>
#include "InputBinding.h"

namespace aq
{
	namespace hid
	{
		class ActionMap
		{
		public:
			// Action → バインディングを追加 (同一 Action に複数登録可 → OR 評価)
			template<typename TAction>
			void Bind(TAction action, InputBinding binding)
			{
				map_[static_cast<uint32_t>(action)].push_back(binding);
			}

			// 指定 Action のバインディングを全削除
			template<typename TAction>
			void Clear(TAction action) { map_.erase(static_cast<uint32_t>(action)); }

			void ClearAll() { map_.clear(); stickMap_.clear(); }

			const std::vector<InputBinding>* Find     (uint32_t actionId) const;

			template<typename TAction>
			void BindStick(TAction action, StickBinding binding)
			{
				stickMap_[static_cast<uint32_t>(action)] = binding;
			}

			template<typename TAction>
			void ClearStick(TAction action) { stickMap_.erase(static_cast<uint32_t>(action)); }

			const StickBinding* FindStick(uint32_t actionId) const;

		private:
			std::unordered_map<uint32_t, std::vector<InputBinding>> map_;
			std::unordered_map<uint32_t, StickBinding>              stickMap_;
		};
	}
}
