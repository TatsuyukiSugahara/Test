#include "aq.h"
#include "Chunk.h"


namespace aq
{
	namespace ecs
	{
		EntityLocation Chunk::CreateEntity(EntityID id)
		{
			if (capacity_ == size_) {
				ResetMemory(capacity_ * 2);
			}
			const auto loc = EntityLocation(size_);
			entityIDs_.push_back(id);
			++size_;
			return loc;
		}


		Chunk::~Chunk()
		{
			if (!begin_) {
				return;
			}
			for (uint32_t entityIndex = 0; entityIndex < size_; ++entityIndex) {
				for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
					const auto dtor = archetype_.GetTypeInfo(i).GetDestructor();
					if (dtor) {
						const auto offset = archetype_.GetOffsetByIndex(i) * capacity_;
						dtor(begin_.get() + offset + archetype_.GetSize(i) * entityIndex);
					}
				}
			}
		}


		EntityID Chunk::DestroyEntitySwap(const EntityLocation& loc)
		{
			// デストラクタ呼び出し
			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto offset = archetype_.GetOffsetByIndex(i) * capacity_;
				const auto currentIndex = offset + archetype_.GetSize(i) * loc.index;
				const auto dtor = archetype_.GetTypeInfo(i).GetDestructor();
				if (dtor) {
					dtor(begin_.get() + currentIndex);
				}
			}

			const uint32_t lastIndex = size_ - 1;
			EntityID swappedId = INVALID_ENTITY_ID;

			// 末尾でなければ末尾 entity を空いたスロットへ移動
			if (loc.index != lastIndex) {
				for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
					const auto offset    = archetype_.GetOffsetByIndex(i) * capacity_;
					const auto compSize  = archetype_.GetSize(i);
					void* dst = begin_.get() + offset + compSize * loc.index;
					void* src = begin_.get() + offset + compSize * lastIndex;

					const auto mover = archetype_.GetTypeInfo(i).GetMover();
					if (mover) {
						// move-construct でオブジェクト寿命を正しく開始し、
						// moved-from 状態になった末尾スロットを destructor で終了させる
						mover(dst, src);
						const auto dtor = archetype_.GetTypeInfo(i).GetDestructor();
						if (dtor) {
							dtor(src);
						}
					} else {
						memcpy(dst, src, compSize);
					}
				}
				swappedId = entityIDs_[lastIndex];
				entityIDs_[loc.index] = swappedId;
			}

			entityIDs_.pop_back();
			--size_;
			return swappedId;
		}


		EntityID Chunk::MoveEntitySlot(EntityLocation& loc, Chunk& other, EntityID entityId)
		{
			// 移動先チャンクに EntityID 付きで作成
			const auto newLoc = other.CreateEntity(entityId);

			// コンポーネントを移動先へ転送
			// 非 trivially-copyable な型は move-construct を使う（src は moved-from になる）
			// その後 DestroyEntitySwap がデストラクタを呼んでも安全
			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto currentOffset = archetype_.GetOffsetByIndex(i) * capacity_;
				const auto currentIndex = currentOffset + archetype_.GetSize(i) * loc.index;

				const auto otherArchetypeIndex = other.archetype_.GetIndexByTypeInfo(archetype_.GetTypeInfo(i));
				EngineAssertMsg(otherArchetypeIndex < other.archetype_.GetArchetypeSize(),
					"MoveEntitySlot: component not found in destination Archetype");
				const auto otherOffset = other.archetype_.GetOffsetByIndex(otherArchetypeIndex) * other.capacity_;
				const auto otherIndex = otherOffset + other.archetype_.GetSize(otherArchetypeIndex) * newLoc.index;

				const auto mover = archetype_.GetTypeInfo(i).GetMover();
				if (mover) {
					mover(other.begin_.get() + otherIndex, begin_.get() + currentIndex);
				} else {
					memcpy(other.begin_.get() + otherIndex, begin_.get() + currentIndex, archetype_.GetSize(i));
				}
			}

			// 移動元を swap-remove（src は moved-from 状態なのでデストラクタは安全）
			const EntityID swappedId = DestroyEntitySwap(loc);
			loc = newLoc;
			return swappedId;
		}


		EntityID Chunk::MoveEntityRemoveArchetype(
			EntityLocation& loc, Chunk& other, EntityID entityId, const TypeInfo& droppedType)
		{
			// ── 1. 移動先スロット確保 ──────────────────────────────────────────────
			const auto newLoc = other.CreateEntity(entityId);

			// ── 2. 各コンポーネントを処理 ──────────────────────────────────────────
			// droppedType → その場でデストラクタ呼び出し（ドロップ）
			// 他の型    → 移動先チャンクへ move-construct
			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i)
			{
				const TypeInfo& typeInfo = archetype_.GetTypeInfo(i);
				const size_t    off      = archetype_.GetOffsetByIndex(i) * capacity_;
				const size_t    csize    = archetype_.GetSize(i);
				void* src = begin_.get() + off + csize * loc.index;

				if (typeInfo == droppedType)
				{
					const auto dtor = typeInfo.GetDestructor();
					if (dtor) dtor(src);
				}
				else
				{
					const size_t dstIdx  = other.archetype_.GetIndexByTypeInfo(typeInfo);
					EngineAssertMsg(dstIdx < other.archetype_.GetArchetypeSize(),
						"MoveEntityRemoveArchetype: component not found in destination Archetype");
					const size_t dstOff  = other.archetype_.GetOffsetByIndex(dstIdx) * other.capacity_;
					const size_t dstSize = other.archetype_.GetSize(dstIdx);
					void* dst = other.begin_.get() + dstOff + dstSize * newLoc.index;
					const auto mover = typeInfo.GetMover();
					if (mover) mover(dst, src);
					else       memcpy(dst, src, csize);
				}
			}

			// ── 3. インライン swap-remove（DestroyEntitySwap は使わない） ───────────
			// 理由: DestroyEntitySwap は全コンポーネントのデストラクタを呼ぶが、
			//       droppedType はステップ2で既に破棄済みのためダブル破棄になる。
			//       非 droppedType は moved-from 状態なので、
			//       デストラクタを呼んでから末尾エンティティを書き込む。
			const uint32_t lastIndex = size_ - 1;
			EntityID       swappedId = INVALID_ENTITY_ID;

			if (loc.index != lastIndex)
			{
				for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i)
				{
					const TypeInfo& typeInfo = archetype_.GetTypeInfo(i);
					const size_t    off      = archetype_.GetOffsetByIndex(i) * capacity_;
					const size_t    csize    = archetype_.GetSize(i);
					void* slot = begin_.get() + off + csize * loc.index;   // 空きスロット
					void* last = begin_.get() + off + csize * lastIndex;   // 末尾エンティティ

					const auto dtor  = typeInfo.GetDestructor();
					const auto mover = typeInfo.GetMover();

					if (typeInfo == droppedType)
					{
						// slot: ステップ2で破棄済み（dead memory）→ move-construct で上書き可
						if (mover)
						{
							mover(slot, last);
							if (dtor) dtor(last);
						}
						else
						{
							memcpy(slot, last, csize);
						}
					}
					else
					{
						// slot: moved-from 状態 → デストラクタで寿命終了してから末尾を移動
						if (dtor) dtor(slot);
						if (mover)
						{
							mover(slot, last);
							if (dtor) dtor(last);
						}
						else
						{
							memcpy(slot, last, csize);
						}
					}
				}
				swappedId = entityIDs_[lastIndex];
				entityIDs_[loc.index] = swappedId;
			}
			else
			{
				// loc.index == lastIndex: スワップ不要、非 droppedType の moved-from を破棄
				for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i)
				{
					const TypeInfo& typeInfo = archetype_.GetTypeInfo(i);
					if (typeInfo == droppedType) continue;   // ステップ2で破棄済み

					const size_t off   = archetype_.GetOffsetByIndex(i) * capacity_;
					const size_t csize = archetype_.GetSize(i);
					void* src = begin_.get() + off + csize * loc.index;
					const auto dtor = typeInfo.GetDestructor();
					if (dtor) dtor(src);
				}
			}

			entityIDs_.pop_back();
			--size_;
			loc = newLoc;
			return swappedId;
		}


		void Chunk::ResetMemory(uint32_t capacity)
		{
			auto work = AllocBuffer(archetype_.GetArchetypeMemorySize() * capacity, archetype_.GetMaxAlign());

			for (size_t i = 0; i < archetype_.GetArchetypeSize(); ++i) {
				const auto offsetBase = archetype_.GetOffsetByIndex(i);
				const auto compSize   = archetype_.GetSize(i);
				const auto mover      = archetype_.GetTypeInfo(i).GetMover();
				const auto dtor       = archetype_.GetTypeInfo(i).GetDestructor();

				for (uint32_t e = 0; e < size_; ++e) {
					void* src = begin_.get() + offsetBase * capacity_ + compSize * e;
					void* dst = work.get()   + offsetBase * capacity  + compSize * e;

					if (mover) {
						mover(dst, src);
						if (dtor) dtor(src);
					} else {
						memcpy(dst, src, compSize);
					}
				}
			}

			capacity_ = capacity;
			begin_ = std::move(work);
		}


		Chunk Chunk::Create(const Archetype& archetype, const uint32_t capacity)
		{
			Chunk result;
			result.capacity_ = capacity;
			result.archetype_ = archetype;
			result.begin_ = AllocBuffer(result.capacity_ * result.archetype_.GetArchetypeMemorySize(), result.archetype_.GetMaxAlign());
			return result;
		}
	}
}
