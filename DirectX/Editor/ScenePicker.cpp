#include "stdafx.h"
#include "ScenePicker.h"
#include "EditorContext.h"
#include "Level/LevelEditModel.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>


namespace editor
{
	namespace
	{
		/** 境界の無いエンティティを位置で選ぶときの、画面の上の距離の上限 [px] */
		static constexpr float POINT_PICK_RADIUS = 8.0f;


		using NodeList = std::vector<std::unique_ptr<aq::ecs::PrefabEditNode>>;


		/** nodes とその子孫の editId を順に渡す */
		void ForEachNodeId(const NodeList& nodes, const std::function<void(uint32_t)>& visit)
		{
			for (const auto& node : nodes) {
				visit(node->editId);
				ForEachNodeId(node->children, visit);
			}
		}


		/** 対応表の対象のノード(トップレベルと 1 段目のインライン subLevel。LevelEditModel の対応表と同じ範囲)の editId を順に渡す */
		void ForEachPreviewNodeId(const aq::level::LevelEditModel& model, const std::function<void(uint32_t)>& visit)
		{
			ForEachNodeId(model.GetEntities(), visit);
			for (const aq::level::SubLevelEdit& sub : model.GetSubLevels()) {
				if (!sub.isInline) continue;
				ForEachNodeId(sub.entities, visit);
			}
		}


		/** 2 つの AABB を合わせた AABB */
		aq::math::AABB Merge(const aq::math::AABB& a, const aq::math::AABB& b)
		{
			const aq::math::Vector3 minPoint(std::min(a.center.x - a.extent.x, b.center.x - b.extent.x),
			                                 std::min(a.center.y - a.extent.y, b.center.y - b.extent.y),
			                                 std::min(a.center.z - a.extent.z, b.center.z - b.extent.z));
			const aq::math::Vector3 maxPoint(std::max(a.center.x + a.extent.x, b.center.x + b.extent.x),
			                                 std::max(a.center.y + a.extent.y, b.center.y + b.extent.y),
			                                 std::max(a.center.z + a.extent.z, b.center.z + b.extent.z));
			return aq::math::AABB::FromMinMax(minPoint, maxPoint);
		}
	}


	void ScenePicker::OnBuildUI(EditorContext& context, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
	                            const bool isHovered, const bool isOverGizmo)
	{
		const ImGuiIO& io = ImGui::GetIO();

		// 押した: 画像の上で、Alt なし(Alt + 左は編集用カメラの回り込み)、ギズモの上でない
		if (io.MouseClicked[ImGuiMouseButton_Left]) {
			isPressing_ = isHovered && !io.KeyAlt && !isOverGizmo;
			pressPosition_ = io.MousePos;
		}

		// 離した: 押した位置から動いていなければクリック(ドラッグは選択にしない)
		if (isPressing_ && io.MouseReleased[ImGuiMouseButton_Left]) {
			isPressing_ = false;
			const float dx = io.MousePos.x - pressPosition_.x;
			const float dy = io.MousePos.y - pressPosition_.y;
			if (dx * dx + dy * dy <= io.MouseDragThreshold * io.MouseDragThreshold) {
				Pick(context, camera, imageMin, imageMax, pressPosition_);
			}
		}
	}


	bool ScenePicker::GetEntityWorldBounds(const aq::ecs::EntityHandle& handle, aq::math::AABB& out)
	{
		aq::ecs::EntityContext& ctx = aq::ecs::EntityContext::Get();
		if (!ctx.IsValid(handle)) return false;

		if (const auto* staticMesh = ctx.GetComponent<aq::ecs::StaticMeshComponent>(handle)) {
			if (staticMesh->GetWorldBounds(out)) return true;
		}
		if (const auto* skeletalMesh = ctx.GetComponent<aq::ecs::SkeletalMeshComponent>(handle)) {
			if (skeletalMesh->GetWorldBounds(out)) return true;
		}
		if (const auto* boxMesh = ctx.GetComponent<aq::ecs::BoxStaticMeshComponent>(handle)) {
			if (boxMesh->GetWorldBounds(out)) return true;
		}
		return false;
	}


	bool ScenePicker::GetEntityWorldPosition(const aq::ecs::EntityHandle& handle, aq::math::Vector3& out)
	{
		aq::ecs::EntityContext& ctx = aq::ecs::EntityContext::Get();
		if (!ctx.IsValid(handle)) return false;

		const auto* hierarchical = ctx.GetComponent<aq::ecs::HierarchicalTransformComponent>(handle);
		if (hierarchical == nullptr) return false;
		out = hierarchical->transform.position;
		return true;
	}


	SceneNodeBounds ScenePicker::ComputeNodeBounds(const aq::level::LevelEditModel& model, const uint32_t editId)
	{
		SceneNodeBounds result;
		const std::vector<aq::ecs::EntityHandle>* entities = model.FindPreviewEntities(editId);
		if (entities == nullptr) return result;

		for (const aq::ecs::EntityHandle& handle : *entities) {
			aq::math::AABB box;
			if (GetEntityWorldBounds(handle, box)) {
				result.bounds    = result.hasBounds ? Merge(result.bounds, box) : box;
				result.hasBounds = true;
			}
			// 位置は根(先頭)を使う。根が消えていたら次の生きているもの
			if (!result.hasPosition) {
				result.hasPosition = GetEntityWorldPosition(handle, result.position);
			}
		}
		return result;
	}


	void ScenePicker::Pick(EditorContext& context, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
	                       const ImVec2& screenPosition)
	{
		const aq::level::LevelEditModel* model = context.GetModel();
		const float width  = imageMax.x - imageMin.x;
		const float height = imageMax.y - imageMin.y;
		if (model == nullptr || width <= 0.0f || height <= 0.0f) return;

		const float u = (screenPosition.x - imageMin.x) / width;
		const float v = (screenPosition.y - imageMin.y) / height;
		const aq::math::Ray ray = camera.ScreenPointToRay(u, v);
		const aq::math::Vector3& eye = camera.GetPosition();

		uint32_t bestId       = 0;
		float    bestDistance = FLT_MAX;
		ForEachPreviewNodeId(*model, [&](const uint32_t editId)
			{
				const std::vector<aq::ecs::EntityHandle>* entities = model->FindPreviewEntities(editId);
				if (entities == nullptr) return;

				for (const aq::ecs::EntityHandle& handle : *entities) {
					// ワールド AABB があればレイとの交差(距離はレイの始点から)
					aq::math::AABB box;
					if (GetEntityWorldBounds(handle, box)) {
						float distance = 0.0f;
						if (aq::math::IntersectRayAABB(ray, box, distance) && distance < bestDistance) {
							bestDistance = distance;
							bestId       = editId;
						}
						continue;
					}

					// 無ければ位置を画面へ写し、クリックの位置から 8 px 以内か(距離はカメラから)
					aq::math::Vector3 position;
					if (!GetEntityWorldPosition(handle, position)) continue;
					const aq::math::Vector4 clip = camera.WorldToClip(position);
					if (clip.w <= 1e-6f || clip.z < 0.0f) continue;   // カメラの後ろ・近い面より手前
					const float pointX = imageMin.x + (clip.x / clip.w * 0.5f + 0.5f) * width;
					const float pointY = imageMin.y + (0.5f - clip.y / clip.w * 0.5f) * height;
					const float dx = pointX - screenPosition.x;
					const float dy = pointY - screenPosition.y;
					if (dx * dx + dy * dy > POINT_PICK_RADIUS * POINT_PICK_RADIUS) continue;

					const float ex = position.x - eye.x;
					const float ey = position.y - eye.y;
					const float ez = position.z - eye.z;
					const float distance = std::sqrt(ex * ex + ey * ey + ez * ez);
					if (distance < bestDistance) {
						bestDistance = distance;
						bestId       = editId;
					}
				}
			});

		if (bestId != 0) {
			context.SetSelectedId(bestId);
		} else {
			context.ClearSelection();
		}
	}
}
