#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "SceneHierarchySystem.h"
#include "ECS/ECS.h"
#include "ECS/EntityContext.h"
#include "ECS/EntityDebugTag.h"
#include "ECS/ComponentRegistry.h"
#include "Component/TransformComponentSystem.h"
#include "Component/HierarchicalTransformComponent.h"
#include <imgui/imgui.h>
#include <cstring>
#include <vector>

namespace aq
{
	namespace ecs
	{
		SceneHierarchySystem* SceneHierarchySystem::instance_ = nullptr;

		SceneHierarchySystem::SceneHierarchySystem()
		{
			instance_ = this;
		}

		SceneHierarchySystem::~SceneHierarchySystem()
		{
			if (instance_ == this) instance_ = nullptr;
		}


		// ── メニュー ─────────────────────────────────────────────────────────────

		void SceneHierarchySystem::DebugRenderMenu()
		{
			if (ImGui::BeginMenu("Scene"))
			{
				ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
				ImGui::MenuItem("Inspector", nullptr, &showInspector_);
				ImGui::EndMenu();
			}
		}


		// ── DebugRender エントリ ──────────────────────────────────────────────────

		void SceneHierarchySystem::DebugRender()
		{
			if (showHierarchy_) DrawHierarchyWindow();
			if (showInspector_) DrawInspectorWindow();
		}


		// ── Hierarchy ウィンドウ ──────────────────────────────────────────────────

		void SceneHierarchySystem::DrawHierarchyWindow()
		{
			if (!ImGui::Begin("Scene Hierarchy", &showHierarchy_))
			{
				ImGui::End();
				return;
			}

			auto& ctx = EntityContext::Get();

			// 新規エンティティ作成
			if (ImGui::Button("+ New Entity"))
			{
				Entity e = ctx.CreateEntity<TransformComponent, HierarchicalTransformComponent>();
				selectedHandle_ = e.GetHandle();
			}

			ImGui::Separator();

			// 全ハンドルを収集
			std::vector<EntityHandle> allHandles;
			Foreach<EntityDebugTag>([&](const Entity& e, EntityDebugTag*)
			{
				allHandles.push_back(e.GetHandle());
			});

			// "World" ラベルをドロップ先にして親子関係を解除
			ImGui::TextDisabled("World");
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HANDLE"))
				{
					EntityHandle dragged;
					memcpy(&dragged, payload->Data, sizeof(EntityHandle));
					ctx.DetachParent(dragged);
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::Separator();

			// Spatial ルート（HTC あり・親なし）
			for (EntityHandle handle : allHandles)
			{
				auto* htc = ctx.GetComponent<HierarchicalTransformComponent>(handle);
				if (!htc) continue;
				if (ctx.IsValid(htc->parentHandle)) continue;
				DrawEntityNode(handle);
			}

			// Other（HTC なし）
			bool hasOther = false;
			for (EntityHandle handle : allHandles)
			{
				if (ctx.GetComponent<HierarchicalTransformComponent>(handle)) continue;
				if (!hasOther)
				{
					ImGui::Separator();
					ImGui::TextDisabled("Other (no transform)");
					hasOther = true;
				}
				DrawEntityNode(handle);
			}

			ImGui::End();
		}


		// ── エンティティノード（再帰） ────────────────────────────────────────────

		void SceneHierarchySystem::DrawEntityNode(EntityHandle handle, int depth)
		{
			constexpr int kMaxDepth = 64;
			if (depth >= kMaxDepth)
			{
				ImGui::TextDisabled("... (max depth)");
				return;
			}

			auto& ctx = EntityContext::Get();
			auto* tag = ctx.GetComponent<EntityDebugTag>(handle);
			auto* htc = ctx.GetComponent<HierarchicalTransformComponent>(handle);

			const char* name = (tag && tag->displayName[0] != '\0') ? tag->displayName : "Entity";

			bool hasChildren = false;
			if (htc)
			{
				for (EntityHandle child : htc->childHandles)
				{
					if (ctx.IsValid(child)) { hasChildren = true; break; }
				}
			}

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			                         | ImGuiTreeNodeFlags_SpanAvailWidth
			                         | ImGuiTreeNodeFlags_DefaultOpen;
			if (selectedHandle_ == handle) flags |= ImGuiTreeNodeFlags_Selected;
			if (!hasChildren)              flags |= ImGuiTreeNodeFlags_Leaf;

			bool opened = ImGui::TreeNodeEx(
				reinterpret_cast<void*>(static_cast<uintptr_t>(handle.id)),
				flags, "%s  [#%u]", name, handle.id);

			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				selectedHandle_ = handle;

			// ドラッグソース
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("ENTITY_HANDLE", &handle, sizeof(EntityHandle));
				ImGui::Text("%s", name);
				ImGui::EndDragDropSource();
			}

			// ドロップターゲット：この Entity の子として付け替え
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HANDLE"))
				{
					EntityHandle dragged;
					memcpy(&dragged, payload->Data, sizeof(EntityHandle));
					if (dragged != handle)
						ctx.SetParent(dragged, handle);
				}
				ImGui::EndDragDropTarget();
			}

			if (opened)
			{
				if (htc)
				{
					for (EntityHandle child : htc->childHandles)
					{
						if (ctx.IsValid(child))
							DrawEntityNode(child, depth + 1);
					}
				}
				ImGui::TreePop();
			}
		}


		// ── Inspector ウィンドウ ──────────────────────────────────────────────────

		void SceneHierarchySystem::DrawInspectorWindow()
		{
			if (!ImGui::Begin("Inspector", &showInspector_))
			{
				ImGui::End();
				return;
			}

			auto& ctx = EntityContext::Get();

			if (!ctx.IsValid(selectedHandle_))
			{
				ImGui::TextDisabled("No entity selected.");
				ImGui::End();
				return;
			}

			// 名前編集 + ID
			auto* tag = ctx.GetComponent<EntityDebugTag>(selectedHandle_);
			if (tag) ImGui::InputText("Name", tag->displayName, sizeof(tag->displayName));
			ImGui::Text("ID: %u", selectedHandle_.id);
			ImGui::Separator();

			// 登録済みコンポーネントのインスペクター
			// 右クリックで "Remove Component" が選択可能（remove != nullptr の場合のみ）
			for (const auto& [typeInfo, meta] : ComponentRegistry::Get().GetAll())
			{
				if (!meta.has(selectedHandle_)) continue;

				ImGui::PushID(static_cast<int>(typeInfo.GetHash()));

				bool isOpen = ImGui::CollapsingHeader(meta.displayName, ImGuiTreeNodeFlags_DefaultOpen);

				if (meta.remove && ImGui::BeginPopupContextItem())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
					if (ImGui::MenuItem("Remove Component"))
						meta.remove(selectedHandle_);
					ImGui::PopStyleColor();
					ImGui::EndPopup();
				}

				if (isOpen) meta.drawInspector(selectedHandle_);

				ImGui::PopID();
			}

			ImGui::Separator();

			// コンポーネント追加
			if (ImGui::Button("+ Add Component"))
				ImGui::OpenPopup("AddComponent");

			if (ImGui::BeginPopup("AddComponent"))
			{
				for (const auto& [typeInfo, meta] : ComponentRegistry::Get().GetAll())
				{
					if (meta.has(selectedHandle_)) continue;
					if (ImGui::MenuItem(meta.displayName))
					{
						meta.add(selectedHandle_);
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndPopup();
			}

			// エンティティ削除
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
			if (ImGui::Button("Destroy Entity"))
				ImGui::OpenPopup("Destroy Entity?");
			ImGui::PopStyleColor();

			if (ImGui::BeginPopupModal("Destroy Entity?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				const char* entityName = (tag && tag->displayName[0] != '\0') ? tag->displayName : "Entity";
				ImGui::Text("Destroy \"%s\" (#%u)?", entityName, selectedHandle_.id);
				ImGui::Separator();

				ImGui::Checkbox("Also destroy children (subtree)", &destroySubtree_);
				if (!destroySubtree_)
					ImGui::TextDisabled("Children will be detached and moved to root.");

				ImGui::Separator();

				if (ImGui::Button("Destroy", ImVec2(120.0f, 0.0f)))
				{
					EntityHandle target = selectedHandle_;
					selectedHandle_ = EntityHandle::InvalidHandle();

					if (destroySubtree_)
					{
						DestroySubtree(target);
					}
					else
					{
						// 子をすべて切り離してルートに移動
						auto* htc = ctx.GetComponent<HierarchicalTransformComponent>(target);
						if (htc)
						{
							std::vector<EntityHandle> children = htc->childHandles;
							for (EntityHandle child : children)
							{
								if (ctx.IsValid(child))
									ctx.DetachParent(child);
							}
						}
						ctx.DetachParent(target);
						ctx.RequestDestroyEntity(target);
					}

					destroySubtree_ = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
				{
					destroySubtree_ = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::End();
		}


		// ── サブツリー削除 ────────────────────────────────────────────────────────

		void SceneHierarchySystem::DestroySubtree(EntityHandle handle)
		{
			auto& ctx = EntityContext::Get();
			if (!ctx.IsValid(handle)) return;

			auto* htc = ctx.GetComponent<HierarchicalTransformComponent>(handle);
			if (htc)
			{
				std::vector<EntityHandle> children = htc->childHandles;
				for (EntityHandle child : children)
					DestroySubtree(child);
			}

			ctx.DetachParent(handle);
			ctx.RequestDestroyEntity(handle);
		}

	}
}
#endif
