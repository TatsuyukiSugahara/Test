#pragma once
#include "IComponent.h"
#include "../EnginePreCompile.h"


namespace engine
{
	namespace ecs
	{
		/**
		 * Chunk内のデータ構造
		 */
		class Archetype
		{
		private:
			TypeInfo typeList_[MAX_COMPONENT_SIZE];
			size_t archetypeMemorySize_;
			size_t archetypeSize_;




		public:
			Archetype() : archetypeMemorySize_(0), archetypeSize_(0) {}

			constexpr bool operator==(const Archetype& other) const
			{
				if (archetypeSize_ != other.archetypeSize_) {
					return false;
				}
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i] != other.typeList_[i]) {
						return false;
					}
				}
				return true;
			}
			constexpr bool operator!=(const Archetype& other) const
			{
				return !(*this == other);
			}




		public:
			/**
			 * 自身が持っているTypeをOtherが全て含んでいるか
			 */
			constexpr bool IsIn(const Archetype& other) const
			{
				for (size_t i = 0; i < archetypeSize_; ++i) {
					bool isIn = false;
					for (size_t j = 0; j < other.archetypeSize_; ++j) {
						if (typeList_[i] == other.typeList_[j]) {
							isIn = true;
							break;
						}
					}
					if (!isIn) {
						return false;
					}
				}
				return true;
			}


			/**
			 * Archetypeを追加
			 */
			template <typename T>
			constexpr Archetype& AddType()
			{
				EngineAssert(archetypeSize_ > 0);
				EngineAssert(archetypeSize_ < ArraySize(typeList_));

				size_t insertIndex = archetypeSize_;
				constexpr auto newType = TypeInfo::Create<T>();
				archetypeMemorySize_ += sizeof(T);
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i].GetHash() > newType.GetHash()) {
						for (size_t j = archetypeSize_; j > i; --j) {
							typeList_[j] = typeList_[j - 1];
						}
						insertIndex = i;
						break;
					}
				}
				typeList_[insertIndex] = newType;
				++archetypeSize_;
				return *this;
			}


			/**
			 * 指定したコンポーネントが登録されているIndex取得
			 */
			template <typename T, typename  std::enable_if_t<IsComponent<T>>>
			constexpr size_t GetIndex() const
			{
				const auto targetType = TypeInfo::Create<T>();
				for (size_t i = 0; archetypeSize_; ++i) {
					if (typeList_[i] == targetType) {
						return i;
					}
				}
				// 登録されていない
				return archetypeSize_;
			}


			/**
			 * 指定したComponentまでのメモリサイズ取得
			 */
			template <typename T, typename = std::enable_if_t<IsComponent<T>>>
			constexpr size_t GetOffset() const
			{
				size_t result = 0;

				const auto targetType = TypeInfo::Create<T>();
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i] == targetType) {
						break;
					}
					result += typeList_[i].GetSize();
				}
				return result;
			}


			/**
			 * Indexまでのメモリサイズを取得
			 */
			constexpr size_t GetOffsetByIndex(const size_t index) const
			{
				size_t result = 0;
				for (size_t i = 0; i < index; ++i) {
					result += typeList_[i].GetSize();
				}
				return result;
			}


			/**
			 * TypeInfoからIndexを取得
			 */
			constexpr size_t GetIndexByTypeInfo(const TypeInfo& info) const
			{
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i] == info) {
						return i;
					}
				}
				// 登録されていない
				return archetypeSize_;
			}


			/**
			 * 指定したIndexのTypeサイズを取得
			 */
			constexpr size_t GetSize(const size_t index) const
			{
				EngineAssert(index < archetypeSize_);
				return typeList_[index].GetSize();
			}

			
			/**
			 * 指定したIndexのTypeInfoを取得
			 */
			constexpr TypeInfo GetTypeInfo(const size_t index) const
			{
				return typeList_[index];
			}


			/**
			 * ArchTypeの数を取得
			 */
			constexpr size_t GetArchetypeSize() const
			{
				return archetypeSize_;
			}


			/**
			 * ArchetypeのByte数を取得
			 */
			constexpr size_t GetArchetypeMemorySize() const
			{
				return archetypeMemorySize_;
			}




		public:
			/**
			 * ComponentからArchetypeを生成
			 */
			template <typename ...Args>
			static constexpr Archetype Create()
			{
				Archetype result;
				result.CreateImpl<Args...>();

				if (result.archetypeSize_ > 1) {
					for (size_t i = 0; i < result.archetypeSize_ - 1; ++i) {
						for (size_t j = i + 1; j < result.archetypeSize_; ++j) {
							if (result.typeList_[i].GetHash() > result.typeList_[j].GetHash()) {
								const auto work = result.typeList_[i];
								result.typeList_[i] = result.typeList_[j];
								result.typeList_[j] = work;
							}
						}
					}
				}

				for (size_t i = 0; i < result.archetypeSize_; ++i) {
					result.archetypeMemorySize_ += result.typeList_[i].GetSize();
				}

				return result;
			}




		private:
			template <typename Head, typename ...Tails, typename = std::enable_if_t<IsComponent<Head>>>
			constexpr void CreateImpl()
			{
				EngineAssert(archetypeSize_ < ArraySize(typeList_));
				typeList_[archetypeSize_] = TypeInfo::Create<Head>();
				++archetypeSize_;
				if constexpr (sizeof...(Tails) != 0) {
					CreateImpl<Tails...>();
				}
			}
		};
	}
}