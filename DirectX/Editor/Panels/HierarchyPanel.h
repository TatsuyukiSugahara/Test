#pragma once
#include "ECS/PrefabEditNodeOps.h"
#include <memory>
#include <vector>


namespace aq
{
	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	class EditorContext;




	/**
	 * ヒエラルキー(編集中の Level のノードの木)。
	 * モデルは const で読むだけで、ツリーの操作要求は同じフレームのうちにノード ID に直してコマンドとして積む
	 */
	class HierarchyPanel
	{
	private:
		using NodeList = std::vector<std::unique_ptr<aq::ecs::PrefabEditNode>>;


	public:
		/** ウィンドウ名。ドッキング配置は editor_layout.ini にこの名前で保存される */
		static constexpr const char* WINDOW_NAME = "Hierarchy";


	public:
		/** ウィンドウを描く。Edit 以外では編集モデルの木を出さない */
		void Draw(EditorContext& context);


	private:
		/** 編集モデルの木(Level 名の見出し、トップレベルの entities、1 段目の subLevel) */
		void DrawModel(EditorContext& context, const aq::level::LevelEditModel& model);

		/**
		 * ルート列の見出し。ドロップ先(その列の末尾へ移す)と、右クリック / "+" の Create Entity を持つ
		 * @param subLevelIndex -1 = トップレベルの entities、0 以上 = その添字のインライン subLevel
		 */
		void DrawRootHeader(EditorContext& context, const aq::level::LevelEditModel& model, const char* label, const int subLevelIndex);

		/** ルート列の木を描き、操作要求を ops へ足す */
		void DrawNodeList(const NodeList& list, const aq::ecs::PrefabTreeDrawOptions& options,
		                  std::vector<aq::ecs::PrefabNodeOp>& ops);


	private:
		/** ツリーの操作要求をノード ID に直し、選択の更新かコマンドにする(要求のポインタはこのフレームのうちだけ有効) */
		static void ApplyTreeOps(EditorContext& context, const std::vector<aq::ecs::PrefabNodeOp>& ops);

		/** node が今のモデルのノードか(ドラッグの荷物のポインタを使う前に、モデルが変わって無効になっていないかを確かめる) */
		static bool ContainsNode(const aq::level::LevelEditModel& model, const aq::ecs::PrefabEditNode* node);

		/** ルート列の末尾に新しい実体ノードを作るコマンドを積む */
		static void SubmitCreateAtRoot(EditorContext& context, const int subLevelIndex);
	};
}
