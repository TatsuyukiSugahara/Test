#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <typeinfo>
#include <utility>
#include <vector>
#include "IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		class RenderPipeline;


		/**
		 * IRenderPass の列を組み立て、検証して RenderPipeline を確定させる(設計書/レンダーパイプライン設計.md §1.3)。
		 *
		 * Add に加え、InsertAfter / InsertBefore / Replace / Remove で既存の列(Standard() 等)を
		 * 組み替えられる(P2)。どれも TAnchor が列に見つからなければログを出して何もせず false を返す。
		 */
		class PipelineBuilder
		{
		private:
			std::vector<std::unique_ptr<IRenderPass>> passes_;


		public:
			/** パスを生成して末尾へ追加する(引数はパスのコンストラクタへそのまま転送する) */
			template <typename TPass, typename... Args>
			PipelineBuilder& Add(Args&&... args)
			{
				passes_.push_back(std::make_unique<TPass>(std::forward<Args>(args)...));
				return *this;
			}

			/** 生成済みのパスを末尾へ追加する */
			PipelineBuilder& Add(std::unique_ptr<IRenderPass> pass);

			/** TAnchor(列の最初の一致)の直後に挿入する。見つからなければログを出して false */
			template <typename TAnchor>
			bool InsertAfter(std::unique_ptr<IRenderPass> pass)
			{
				const size_t index = FindIndexOf<TAnchor>();
				if (index == passes_.size()) {
					LogMissingAnchor(typeid(TAnchor).name());
					return false;
				}
				InsertAtIndex(index + 1, std::move(pass));
				return true;
			}

			/** TAnchor(列の最初の一致)の直前に挿入する。見つからなければログを出して false */
			template <typename TAnchor>
			bool InsertBefore(std::unique_ptr<IRenderPass> pass)
			{
				const size_t index = FindIndexOf<TAnchor>();
				if (index == passes_.size()) {
					LogMissingAnchor(typeid(TAnchor).name());
					return false;
				}
				InsertAtIndex(index, std::move(pass));
				return true;
			}

			/** TAnchor(列の最初の一致)を置き換える。見つからなければログを出して false */
			template <typename TAnchor>
			bool Replace(std::unique_ptr<IRenderPass> pass)
			{
				const size_t index = FindIndexOf<TAnchor>();
				if (index == passes_.size()) {
					LogMissingAnchor(typeid(TAnchor).name());
					return false;
				}
				ReplaceAtIndex(index, std::move(pass));
				return true;
			}

			/** TAnchor(列の最初の一致)を取り除く。見つからなければログを出して false */
			template <typename TAnchor>
			bool Remove()
			{
				const size_t index = FindIndexOf<TAnchor>();
				if (index == passes_.size()) {
					LogMissingAnchor(typeid(TAnchor).name());
					return false;
				}
				RemoveAtIndex(index);
				return true;
			}

			/**
			 * 列を検証して確定する。
			 *  1. IsSupported() が false のパスを除外する(ログのみ)
			 *  2. Reads が「初期状態(Scene)+ それまでの Writes」に無ければログを出して失敗(nullptr)
			 *  3. Frame scope が View scope の列の途中に挟まっていればログを出して失敗(nullptr)
			 *  4. 各パスの Setup を順に呼ぶ。false を返したらログを出して nullptr を返す
			 *  5. 確定した列をログに 1 行出す
			 */
			std::unique_ptr<RenderPipeline> Build(const uint32_t width, const uint32_t height);


		private:
			/** dynamic_cast で列の先頭から TAnchor を探す。無ければ passes_.size() */
			template <typename TAnchor>
			size_t FindIndexOf() const
			{
				for (size_t i = 0; i < passes_.size(); ++i) {
					if (dynamic_cast<TAnchor*>(passes_[i].get()) != nullptr) {
						return i;
					}
				}
				return passes_.size();
			}

			static void LogMissingAnchor(const char* typeName);
			void InsertAtIndex(const size_t index, std::unique_ptr<IRenderPass> pass);
			void ReplaceAtIndex(const size_t index, std::unique_ptr<IRenderPass> pass);
			void RemoveAtIndex(const size_t index);
		};
	}
}
