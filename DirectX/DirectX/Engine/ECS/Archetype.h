#pragma once
#include "IComponent.h"
#include <algorithm>


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
			/** 特別に初期化 */
			size_t archetypeMemorySize_ = 0;
			size_t archetypeSize_ = 0;
			// 全コンポーネントのアライメントの最大値（SOA ブロック境界の計算に使用）
			size_t maxAlign_ = 1;




		public:
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
			 * 指定した型が登録されているか
			 */
			template <typename T>
			constexpr bool HasType() const
			{
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i] == TypeInfo::Create<T>()) {
						return true;
					}
				}
				return false;
			}


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
				size_t insertIndex = archetypeSize_;
				constexpr auto newType = TypeInfo::Create<T>();
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

				// maxAlign_ を更新してから archetypeMemorySize_ を再計算
				maxAlign_ = std::max(maxAlign_, newType.GetAlign());
				archetypeMemorySize_ = 0;
				for (size_t i = 0; i < archetypeSize_; ++i) {
					archetypeMemorySize_ += AlignUp(typeList_[i].GetSize(), maxAlign_);
				}
				return *this;
			}


			/**
			 * 指定したコンポーネントが登録されているIndex取得
			 */
			template <typename T>
			constexpr size_t GetIndex() const
			{
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i] == TypeInfo::Create<T>()) {
						return i;
					}
				}
				// 登録されていない
				return archetypeSize_;
			}


			/**
			 * 指定したComponentまでのメモリサイズ取得（padding 込み）
			 */
			template <typename T>
			constexpr size_t GetOffset() const
			{
				size_t result = 0;
				for (size_t i = 0; i < archetypeSize_; ++i) {
					if (typeList_[i] == TypeInfo::Create<T>()) {
						break;
					}
					result += AlignUp(typeList_[i].GetSize(), maxAlign_);
				}
				return result;
			}


			/**
			 * Indexまでのメモリサイズを取得（padding 込み）
			 * GetOffsetByIndex(i) * capacity がコンポーネント i の SOA ブロック先頭バイトになる
			 */
			constexpr size_t GetOffsetByIndex(const size_t index) const
			{
				size_t result = 0;
				for (size_t i = 0; i < index; ++i) {
					result += AlignUp(typeList_[i].GetSize(), maxAlign_);
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
			 * ArchetypeのByte数を取得（padding 込み、capacity 倍すると SOA バッファ全体サイズになる）
			 */
			constexpr size_t GetArchetypeMemorySize() const
			{
				return archetypeMemorySize_;
			}


			constexpr size_t GetMaxAlign() const
			{
				return maxAlign_;
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

				// ソート後に maxAlign_ を確定してから archetypeMemorySize_ を計算
				for (size_t i = 0; i < result.archetypeSize_; ++i) {
					result.maxAlign_ = std::max(result.maxAlign_, result.typeList_[i].GetAlign());
				}
				for (size_t i = 0; i < result.archetypeSize_; ++i) {
					result.archetypeMemorySize_ += AlignUp(result.typeList_[i].GetSize(), result.maxAlign_);
				}

				return result;
			}




		private:
			template <typename Head, typename ...Tails>
			constexpr void CreateImpl()
			{
				typeList_[archetypeSize_] = TypeInfo::Create<Head>();
				++archetypeSize_;
				if constexpr (sizeof...(Tails) != 0) {
					CreateImpl<Tails...>();
				}
			}

			// アライメント切り上げ（align は 2 の冪を前提とする）
			static constexpr size_t AlignUp(size_t value, size_t align)
			{
				return (value + align - 1) & ~(align - 1);
			}
		};
	}
}
