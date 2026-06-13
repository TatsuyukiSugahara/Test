#include "../EnginePreCompile.h"
#include "Chunk.h"


namespace engine
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
