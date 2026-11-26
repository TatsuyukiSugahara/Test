#pragma once
#include <imgui/imgui.h>
#include <cstdint>


namespace aq
{
	class Camera;

	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	class EditorContext;


	/**
	 * ノードのワールドの境界(選択の箱・F・クリック選択で使う)
	 */
	struct SceneNodeBounds
	{
		/** そのノードのエンティティ群のワールド AABB を合わせたもの(hasBounds のときだけ有効) */
		aq::math::AABB bounds;
		bool           hasBounds = false;

		/** 根のエンティティのワールドの位置(hasPosition のときだけ有効) */
		aq::math::Vector3 position = aq::math::Vector3(0.0f, 0.0f, 0.0f);
		bool              hasPosition = false;
	};




	/**
	 * シーンビューのクリック選択(設計書/エディタ/E11_シーンビュー.md 6.)。
	 * Edit のとき、ゲームビューの画像の上で左クリック(ドラッグでない、Alt なし、ギズモの上でない)したら、
	 * 編集モデルの対応表(LevelEditModel::FindPreviewEntities)のすべてのエンティティをレイで調べ、最も近いもののノードを選ぶ。
	 * 何も当たらなければ選択を外す。エンティティは読むだけで、書き換えない
	 */
	class ScenePicker
	{
	private:
		/** 画像の上で左ボタンを押し、まだ離していない(離したときにドラッグでなかったかを確かめる) */
		bool   isPressing_ = false;
		ImVec2 pressPosition_;


	public:
		/**
		 * 入力を読み、クリックなら選択を変える(Edit のときだけ OnBuildUI から呼ぶ)。エディタ用コンテキストが current であること。
		 * @param context      選択を書く先(モデルもここから引く)
		 * @param camera       画像を描いたカメラ(このフレームのメインカメラ)
		 * @param imageMin     画像の左上(スクリーン座標)
		 * @param imageMax     画像の右下(スクリーン座標)
		 * @param isHovered    画像の上にマウスがある(ほかの操作中でない)
		 * @param isOverGizmo  ギズモの上にマウスがある(E11c で使う。今は常に false)
		 */
		void OnBuildUI(EditorContext& context, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
		               const bool isHovered, const bool isOverGizmo);

		/** 押している途中のクリックを取り消す(Edit でなくなった・画像が見えなくなったとき) */
		inline void CancelClick() { isPressing_ = false; }


	public:
		/**
		 * エンティティのワールド AABB。持つ描画のコンポーネント(StaticMesh / SkeletalMesh / BoxStaticMesh)を順に試す。
		 * どれも無い・読み込み前なら false(InstancedStaticMesh は対象外。設計 5.)
		 */
		static bool GetEntityWorldBounds(const aq::ecs::EntityHandle& handle, aq::math::AABB& out);

		/** エンティティのワールドの位置(HierarchicalTransformComponent)。無ければ false */
		static bool GetEntityWorldPosition(const aq::ecs::EntityHandle& handle, aq::math::Vector3& out);

		/** editId のノードのエンティティ群の境界と根の位置。プレビューに無いノードは両方 false */
		static SceneNodeBounds ComputeNodeBounds(const aq::level::LevelEditModel& model, const uint32_t editId);


	private:
		/** screenPosition(スクリーン座標)の下にあるノードを選ぶ。何も当たらなければ選択を外す */
		static void Pick(EditorContext& context, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
		                 const ImVec2& screenPosition);
	};
}
