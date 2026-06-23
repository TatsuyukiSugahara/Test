#pragma once
#include <memory>
#include <vector>
#include "UIRenderItem.h"
#include "UIRenderPipeline.h"

namespace aq
{
	namespace rendering
	{
		class RenderCommandList;
		struct FrameContext;
	}

	namespace ui
	{
		class UIScreenManager;


		// UIBatchRenderer: 毎フレーム UIObject ツリーを走査して UIRenderItem を収集し、
		// ソート・VB 構築・RenderCommandList へのコマンド積みを行う。
		// CollectRenderItems() → BuildCommandList() の順で呼ぶ。
		class UIBatchRenderer
		{
		public:
			UIBatchRenderer();
			~UIBatchRenderer();

			// Application::OnUpdate() の末尾で呼ぶ: UIObject ツリーから描画データを収集
			void CollectRenderItems(UIScreenManager& screens);

			// Renderer::BuildCommandList() から呼ばれる: コマンドを積む
			void BuildCommandList(rendering::RenderCommandList& cmdList);

			// 収集済みアイテムをクリア (フレーム開始時)
			void Clear();

		private:
			// UIObject ツリーを再帰走査して items_ に追加
			void CollectFromObject(class UIObject* obj, uint8_t canvasZ);

			// ソート: Canvas 順 → sibling 順 → drawOrder
			void SortItems();

			// グループ化してドローコール数を最小化
			// 同 (shaderType, texture) が連続する区間を 1 DrawCall にまとめる
			void BuildBatches(rendering::RenderCommandList& cmdList);

			std::vector<UIRenderItem>        items_;
			std::unique_ptr<UIRenderPipeline> pipeline_;
		};

	} // namespace ui
} // namespace aq
