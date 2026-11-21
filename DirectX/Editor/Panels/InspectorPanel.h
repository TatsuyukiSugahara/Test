#pragma once
#include "ECS/AlignedStorage.h"
#include "Util/SimpleJson.h"
#include <cstdint>
#include <string>
#include <vector>


namespace aq
{
	namespace ecs
	{
		struct FieldEditEvent;
		struct PrefabEditNode;
	}

	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	class EditorContext;




	/**
	 * インスペクター(選択中のノードの名前とコンポーネントの値)。
	 * モデルは const で読むだけで、値を書くのは自分の編集用の写しだけ。
	 * 確定した変更はコマンドとして積み、モデルへの反映は次のフレームの先頭で行われる
	 */
	class InspectorPanel
	{
	private:
		/** 編集用の写し(コンポーネント 1 つ分) */
		struct ComponentCopy
		{
			std::string             typeName;       // 登録されていない型でも JSON のキーを持つ
			aq::ecs::AlignedStorage data;           // 作れなかった(登録されていない・型消去の関数が無い)ときは空
			std::string             readOnlyText;   // drawEditPtr の無い型: 整形した JSON
		};

		/** 開いている操作(Begin を受け取り、Commit / Cancel がまだ) */
		struct OpenEdit
		{
			uint32_t            nodeId = 0;
			std::string         typeName;
			std::string         key;
			aq::util::JsonValue before;   // Begin の値(変更前)
		};


	private:
		/** 編集用の写しと、作ったときの対象・版番号 */
		std::vector<ComponentCopy> copies_;
		uint32_t                   copiesNodeId_  = 0;
		uint32_t                   copiesVersion_ = 0;
		bool                       copiesValid_   = false;   // false なら次に描くときに作り直す

		/** 参照ノードの解決に失敗した(作り直すまで再試行しない) */
		bool        resolveFailed_ = false;
		std::string resolveError_;

		/** 開いている操作 */
		std::vector<OpenEdit> openEdits_;

		/** このフレームに drawEditPtr を呼んだノードと型(開いている操作が描かれたかの判定に使う) */
		uint32_t                 drawnNodeId_ = 0;
		std::vector<std::string> drawnTypes_;

		/** 名前の入力 */
		char     nameBuffer_[256] = {};
		uint32_t nameNodeId_      = 0;       // nameBuffer_ の対象(0 = 無し)
		bool     isNameActive_    = false;   // 前のフレームで名前の入力が操作中だったか
		bool     isNameDrawn_     = false;   // このフレームに名前の入力を描いたか


	public:
		/** ウィンドウ名。ドッキング配置は editor_layout.ini にこの名前で保存される */
		static constexpr const char* WINDOW_NAME = "Inspector";


	public:
		/** ウィンドウを描く。Edit 以外では編集モデルを出さない */
		void Draw(EditorContext& context);


	private:
		/** 選択中のノードの中身(名前、参照先、コンポーネント、Add Component) */
		void DrawNode(EditorContext& context, const aq::level::LevelEditModel& model, const aq::ecs::PrefabEditNode& node,
		              const uint32_t nodeId);

		/** 名前の入力。Enter か項目から外れたときに、値が変わっていれば RenameNodeCommand を積む */
		void DrawName(EditorContext& context, const aq::ecs::PrefabEditNode& node, const uint32_t nodeId);

		/** 前のフレームまで入力中だった名前を確定する(入力が描かれなくなった・対象が変わったとき) */
		void FinishNameEdit(EditorContext& context);

		/** コンポーネントごとの見出しと中身 */
		void DrawComponents(EditorContext& context, const aq::ecs::PrefabEditNode& node, const uint32_t nodeId);

		/** Add Component のボタンと、分類ごとのサブメニュー(実体ノードだけ) */
		void DrawAddComponent(EditorContext& context, const aq::ecs::PrefabEditNode& node, const uint32_t nodeId);

		/** 写しをモデルから作り直す */
		void RebuildCopies(const EditorContext& context, const aq::level::LevelEditModel& model, const aq::ecs::PrefabEditNode& node,
		                   const uint32_t nodeId);

		/** 1 つのコンポーネントのこのフレームの編集イベントを、控えとコマンドにする */
		void HandleEditEvents(EditorContext& context, const uint32_t nodeId, const std::string& typeName,
		                      std::vector<aq::ecs::FieldEditEvent>& events);

		/** 開いている操作のうち、このフレームに描かれなかったものを捨て、写しも作り直させる */
		void DiscardUndrawnEdits();

		/** 開いている操作の位置(無ければ openEdits_.end()) */
		std::vector<OpenEdit>::iterator FindOpenEdit(const uint32_t nodeId, const std::string& typeName, const std::string& key);
	};
}
