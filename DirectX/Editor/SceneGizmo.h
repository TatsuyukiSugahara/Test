#pragma once
#include "Level/LevelId.h"
#include <imgui/imgui.h>
#include <cstdint>


namespace aq
{
	class Camera;
}


namespace editor
{
	class EditorContext;


	/**
	 * シーンビューのギズモ(設計書/エディタ/E11_シーンビュー.md 7.)。ImGuizmo で選んだノードの Transform を動かす。
	 * ドラッグ中は対応表(LevelEditModel::FindPreviewEntities)の根のエンティティの TransformComponent を直接書き換え(モデルは変えない)、
	 * 離したときに変わったキーだけをコマンドにして 1 回で積む。Esc・選択の変更・Play・ウィンドウからの離脱では書き戻して何も積まない。
	 * プレビューが作り直されたら書き戻さずにやめ、モデルの版だけが変わったら書き戻してやめる。
	 * エンティティの書き換えは OnBuildUI の中(ECS の反復の外)だけで行い、構造は変えない
	 */
	class SceneGizmo
	{
	private:
		/** 操作の種類(W / E / R) */
		enum class Operation : uint8_t
		{
			Translate,
			Rotate,
			Scale,
		};


	private:
		/** 操作の種類と座標系(拡縮は ImGuizmo が常にローカルで行う) */
		Operation operation_ = Operation::Translate;
		bool      isWorld_   = true;

		/** このフレームにマウスがギズモの上にある、またはドラッグ中(クリック選択へ渡す) */
		bool isOverGizmo_ = false;

		/** ImGuizmo に渡す行列(ドラッグ中は ImGuizmo が書き換えるのでフレームをまたいで持つ。それ以外は毎フレーム組み直す) */
		aq::math::Matrix4x4 gizmoMatrix_;

		/** ドラッグの状態 */
		bool                  isDragging_        = false;
		bool                  hasManipulated_    = false;   // ImGuizmo が行列を変えた(押しただけなら積まない)
		uint32_t              dragNodeId_        = 0;
		aq::ecs::EntityHandle dragEntity_;                  // そのノードの根のエンティティ
		aq::level::LevelId    dragPreviewId_;               // 開始のときのプレビュー(変わったら書き戻さない)
		uint32_t              dragVersion_       = 0;       // 開始のときのモデルの版(変わったら書き戻す)
		bool                  isReferenceNode_   = false;
		bool                  modelHasTransform_ = false;   // モデルのノードが Transform を持つ(実体ノードで無ければ確定で足す)
		bool                  hasParent_         = false;
		aq::ecs::Transform    parentWorld_;                 // 開始のときの親のワールド(ドラッグ中は親は動かない)
		aq::ecs::Transform    originalLocal_;               // 開始のときのローカル(書き戻す値)
		aq::ecs::Transform    currentLocal_;                // いま書いているローカル(確定する値)

		/** 参照先に Transform が無いのでギズモを出さないノード(選択・プレビュー・版のどれかが変わるまで) */
		uint32_t           hiddenNodeId_  = 0;
		aq::level::LevelId hiddenPreviewId_;
		uint32_t           hiddenVersion_ = 0;


	public:
		/**
		 * 画像の右上に移動・回転・拡縮とワールド / ローカルの切り替えのボタンを置く(Edit のとき、ゲームビューの入力用ボタンより前に呼ぶ。
		 * 先に置いた項目がホバーを取るので、ボタンの上では入力用ボタンがホバーにならない)
		 */
		void DrawToolbar(const ImVec2& imageMin, const ImVec2& imageMax);

		/**
		 * ギズモを描き、ドラッグを進める(Edit のときだけ OnBuildUI から呼ぶ。ゲームビューのウィンドウの中、入力用ボタンより前)。
		 * @param context        選択とモデル、確定したコマンドを積む先
		 * @param camera         画像を描いたカメラ(このフレームのメインカメラ)
		 * @param imageMin       画像の左上(スクリーン座標。ImGuizmo の描画範囲)
		 * @param imageMax       画像の右下
		 * @param inputButtonId  ゲームビューの入力用ボタンの ID(前のフレームのそのボタンのホバーではギズモの開始を止めない)
		 * @param allowActivate  ドラッグを始めてよい(Alt なし・編集用カメラのドラッグ中でない)
		 */
		void Manipulate(EditorContext& context, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
		                const ImGuiID inputButtonId, const bool allowActivate);

		/** W / E / R で操作を切り替える(画像の上にマウスがあり、編集用カメラの右ドラッグ中でないとき) */
		void HandleShortcuts(const bool isHovered, const bool isCameraFlying);

		/** ドラッグをやめる(Edit でなくなる・Play の要求・画像が見えなくなったとき)。プレビューが同じなら元の値を書き戻す */
		void Cancel(const EditorContext& context);

		/** マウスがギズモの上にある、またはドラッグ中(クリック選択を始めない) */
		inline bool IsOverGizmo() const { return isOverGizmo_; }


	private:
		/** ImGuizmo が IsUsing になった。対象とモデルの値を控える。ギズモを出さないノードなら false */
		bool BeginDrag(const EditorContext& context, const uint32_t nodeId, const aq::ecs::EntityHandle& entity);

		/** ImGuizmo が書き換えた行列をローカルへ直してエンティティへ書く */
		void ApplyMatrix();

		/** 離した。変わったキーだけをコマンドにして積む */
		void CommitDrag(EditorContext& context);

		/** ドラッグをやめる。writeBack なら元の値をエンティティへ書き戻す */
		void EndDrag(const bool writeBack);

		/** エンティティの TransformComponent へ書く(エンティティが消えていれば何もしない) */
		void WriteLocal(const aq::ecs::Transform& local) const;
	};
}
