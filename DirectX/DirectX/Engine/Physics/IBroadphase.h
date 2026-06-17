#pragma once
#include <functional>


namespace aq
{
	namespace physics
	{
		class GhostBody;


		/**
		 * Broadphase（広域衝突判定）の抽象インターフェース。
		 * アルゴリズムを差し替えるには IBroadphase を実装したクラスを
		 * GhostBodyManager::Initialize() / SetBroadphase() に渡す。
		 */
		class IBroadphase
		{
		protected:
			using PairCallback = std::function<void(GhostBody* a, GhostBody* b)>;


		public:
			virtual ~IBroadphase() = default;

			virtual void Add    (GhostBody* body) = 0;
			virtual void Remove (GhostBody* body) = 0;
			virtual void Update (GhostBody* body) = 0;

			/** ペア候補を列挙し callback を呼び出す */
			virtual void Perform(PairCallback callback) = 0;
		};
	}
}
