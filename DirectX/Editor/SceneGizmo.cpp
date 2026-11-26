#include "stdafx.h"
#include "SceneGizmo.h"
#include "EditorContext.h"
#include "GizmoMath.h"
#include "Commands/ComponentCommands.h"
#include "Commands/CompositeCommand.h"
#include "ECS/JsonFieldVisitor.h"
#include "Level/LevelEditModel.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // ImGuiContext::HoveredIdPreviousFrame(ギズモの開始の判定。Manipulate を参照)
#include <imguizmo/ImGuizmo.h>
#include <cmath>
#include <memory>
#include <vector>


namespace editor
{
	namespace
	{
		/** 編集するコンポーネントの型とキー(TransformComponent の Reflect の永続キー) */
		static constexpr const char* TRANSFORM_TYPE_NAME = "Transform";
		static constexpr const char* POSITION_KEY        = "position";
		static constexpr const char* ROTATION_KEY        = "rotation";
		static constexpr const char* SCALE_KEY           = "scale";

		/** 2 つ以上のキーをまとめたコマンドの名前 */
		static constexpr const char* COMPOSITE_COMMAND_NAME = "Transform";

		/** スナップ(Ctrl。Mac は Command):移動 1 m・回転 15°・拡縮 0.1 */
		static constexpr float SNAP_TRANSLATION = 1.0f;
		static constexpr float SNAP_ROTATION    = 15.0f;
		static constexpr float SNAP_SCALE       = 0.1f;

		/**
		 * 分解と逆算の誤差を「変わっていない」とみなす幅。位置と拡縮は値の大きさに対する割合(1 未満は絶対値)、
		 * 回転はクォータニオンの成分の差(同じ回転の符号違いはそろえてから比べる)
		 */
		static constexpr float CHANGE_EPSILON = 1e-5f;

		/** 画像の右上のボタンの余白 */
		static constexpr float TOOLBAR_MARGIN = 8.0f;


		/** 値がほぼ同じか(誤差の幅は CHANGE_EPSILON) */
		bool NearlyEqual(const float a, const float b)
		{
			return std::fabs(a - b) <= CHANGE_EPSILON * std::fmax(1.0f, std::fabs(b));
		}


		bool NearlyEqual(const aq::math::Vector3& a, const aq::math::Vector3& b)
		{
			return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z);
		}


		/** 同じ回転か(q と -q は同じ回転なので、符号をそろえてから比べる) */
		bool NearlyEqual(const aq::math::Quaternion& a, const aq::math::Quaternion& b)
		{
			const float sign = (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0.0f ? -1.0f : 1.0f;
			return std::fabs(a.x * sign - b.x) <= CHANGE_EPSILON && std::fabs(a.y * sign - b.y) <= CHANGE_EPSILON
			    && std::fabs(a.z * sign - b.z) <= CHANGE_EPSILON && std::fabs(a.w * sign - b.w) <= CHANGE_EPSILON;
		}


		/** ビット単位で同じか(ApplyMatrix が変わっていない成分を元の値のまま残すので、確定ではこれで比べる) */
		bool SameBits(const aq::math::Vector4& a, const aq::math::Vector4& b)
		{
			return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
		}


		bool SameBits(const aq::math::Vector3& a, const aq::math::Vector3& b)
		{
			return a.x == b.x && a.y == b.y && a.z == b.z;
		}


		/** ImGuizmo の操作中の状態を捨てる(Enable(false) は編集中のギズモの using を外す。描き方を戻すため直後に有効へ戻す) */
		void ResetImGuizmo()
		{
			ImGuizmo::Enable(false);
			ImGuizmo::Enable(true);
		}


		/** 右上のボタン 1 つ。選ばれているものは押した色にする。押したら true */
		bool ToolbarButton(const char* label, const bool isSelected)
		{
			if (isSelected) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			const bool pressed = ImGui::SmallButton(label);
			if (isSelected) {
				ImGui::PopStyleColor();
			}
			return pressed;
		}
	}


	void SceneGizmo::DrawToolbar(const ImVec2& imageMin, const ImVec2& imageMax)
	{
		// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
		static constexpr const char* MOVE_LABEL   = "Move [W]";
		static constexpr const char* ROTATE_LABEL = "Rotate [E]";
		static constexpr const char* SCALE_LABEL  = "Scale [R]";
		const char* spaceLabel = isWorld_ ? "World" : "Local";

		// 右寄せにするため、先に幅を測る(SmallButton は縦の余白が 0 で、横は FramePadding.x)
		const ImGuiStyle& style = ImGui::GetStyle();
		const char* labels[] = { MOVE_LABEL, ROTATE_LABEL, SCALE_LABEL, spaceLabel };
		float width = 0.0f;
		for (const char* label : labels) {
			width += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
		}
		width += style.ItemSpacing.x * 3.0f;

		const float x = imageMax.x - TOOLBAR_MARGIN - width;
		const float y = imageMin.y + TOOLBAR_MARGIN;
		if (x < imageMin.x + TOOLBAR_MARGIN || y + ImGui::GetTextLineHeight() > imageMax.y) return;   // 画像が小さすぎる

		ImGui::PushID("SceneGizmoToolbar");
		// ドラッグ中は切り替えない(操作の種類が途中で変わると ImGuizmo の状態と合わなくなる)
		ImGui::BeginDisabled(isDragging_);
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		if (ToolbarButton(MOVE_LABEL, operation_ == Operation::Translate)) operation_ = Operation::Translate;
		ImGui::SameLine();
		if (ToolbarButton(ROTATE_LABEL, operation_ == Operation::Rotate)) operation_ = Operation::Rotate;
		ImGui::SameLine();
		if (ToolbarButton(SCALE_LABEL, operation_ == Operation::Scale)) operation_ = Operation::Scale;
		ImGui::SameLine();
		if (ImGui::SmallButton(spaceLabel)) isWorld_ = !isWorld_;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("Gizmo space (Scale is always local)");
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	}


	void SceneGizmo::Manipulate(EditorContext& context, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
	                            const ImGuiID inputButtonId, const bool allowActivate)
	{
		isOverGizmo_ = false;

		const aq::level::LevelEditModel* model = context.GetModel();
		const uint32_t           selectedId = context.GetSelectedId();
		const aq::level::LevelId previewId  = (model != nullptr) ? model->GetPreviewLevelId() : aq::level::LevelId();
		const uint32_t           version    = context.GetVersion();

		// ドラッグの途中で状態が変わったらやめる。
		// プレビューが作り直された(Undo / Redo・ファイル操作・別のパネルのコマンド)なら、控えたエンティティは捨てられているので書き戻さない。
		// 版だけが変わった(Save)・選択が変わった・Esc・アプリが裏に回ったなら、元の値を書き戻す(画面をモデルの値と一致させる)
		if (isDragging_) {
			const ImGuiIO& io = ImGui::GetIO();
			if (model == nullptr || previewId != dragPreviewId_) {
				EndDrag(false);
			} else if (version != dragVersion_ || selectedId != dragNodeId_ || io.AppFocusLost
			        || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				EndDrag(true);
			}
		}

		// ギズモを出さないノードの印は、選択・プレビュー・版のどれかが変わったら外す
		if (hiddenNodeId_ != 0 && (hiddenNodeId_ != selectedId || hiddenPreviewId_ != previewId || hiddenVersion_ != version)) {
			hiddenNodeId_ = 0;
		}

		if (model == nullptr || selectedId == 0 || selectedId == hiddenNodeId_) return;

		// 対象はそのノードの根のエンティティ(参照ノードは展開した部分木の根)
		const std::vector<aq::ecs::EntityHandle>* entities = model->FindPreviewEntities(selectedId);
		if (entities == nullptr || entities->empty()) return;
		const aq::ecs::EntityHandle root = entities->front();
		aq::ecs::EntityContext& ctx = aq::ecs::EntityContext::Get();
		if (!ctx.IsValid(root)) return;
		const auto* hierarchical = ctx.GetComponent<aq::ecs::HierarchicalTransformComponent>(root);
		if (hierarchical == nullptr || ctx.GetComponent<aq::ecs::TransformComponent>(root) == nullptr) return;

		// ドラッグ中でなければ、根のワールドの位置・回転・拡縮から S·R·T を組み直す(ドラッグ中は ImGuizmo が書き換えた行列を渡し続ける)
		if (!isDragging_) {
			gizmoMatrix_ = GizmoMath::ComposeMatrix(hierarchical->transform);
		}

		const float width  = imageMax.x - imageMin.x;
		const float height = imageMax.y - imageMin.y;
		if (width <= 0.0f || height <= 0.0f) return;

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		float snapValue = SNAP_TRANSLATION;
		switch (operation_) {
		case Operation::Translate: operation = ImGuizmo::TRANSLATE; snapValue = SNAP_TRANSLATION; break;
		case Operation::Rotate:    operation = ImGuizmo::ROTATE;    snapValue = SNAP_ROTATION;    break;
		case Operation::Scale:     operation = ImGuizmo::SCALE;     snapValue = SNAP_SCALE;       break;
		}
		const float  snap[3]   = { snapValue, snapValue, snapValue };
		const float* snapOrNull = ImGui::GetIO().KeyCtrl ? snap : nullptr;   // ConfigMacOSXBehaviors では KeyCtrl が Command

		// 行列はエンジンのもの(左手系・行ベクトル・行優先)をそのまま渡す。ImGuizmo も行ベクトル・平行移動が [12]〜[14] の並びで計算する。
		// 左手系は射影の m[2][3] が正であることから ImGuizmo が見分け(回転の輪の描き始め)、深度 [0, 1] はレイの計算(近い方の端を始点にする)と
		// カメラの後ろの判定(クリップ空間の z < 0.001)のどちらにも影響しない
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(imageMin.x, imageMin.y, width, height);

		// ImGuizmo は「どの項目もホバーされていない」ときだけドラッグを始める(ImGui::IsAnyItemHovered は前のフレームのホバーも見る)。
		// 画像には入力用の見えないボタンが重なっていて前のフレームのホバーを常に取っているので、そのボタンの分だけ見なかったことにする。
		// 始めてはいけないとき(Alt + 左は編集用カメラの回り込み、カメラのドラッグ中)は逆にホバーがあったことにして止める。
		// どちらも Manipulate の間だけで、直後に戻す
		ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
		const ImGuiID savedHoveredPrevious = imguiContext.HoveredIdPreviousFrame;
		if (allowActivate) {
			if (savedHoveredPrevious == inputButtonId) imguiContext.HoveredIdPreviousFrame = 0;
		} else if (savedHoveredPrevious == 0) {
			imguiContext.HoveredIdPreviousFrame = inputButtonId;
		}
		const bool manipulated = ImGuizmo::Manipulate(&camera.GetViewMatrix().m[0][0], &camera.GetProjectionMatrix().m[0][0],
		                                              operation, isWorld_ ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
		                                              &gizmoMatrix_.m[0][0], nullptr, snapOrNull);
		imguiContext.HoveredIdPreviousFrame = savedHoveredPrevious;

		const bool isUsing = ImGuizmo::IsUsing();
		if (!isDragging_ && isUsing) {
			if (!BeginDrag(context, selectedId, root)) {
				// 参照先に Transform が無い(ギズモを出さない)
				ResetImGuizmo();
				return;
			}
		}

		if (isDragging_) {
			if (manipulated) {
				hasManipulated_ = true;
				ApplyMatrix();
			}
			// 離した(ImGuizmo は離したフレームの Manipulate の中で using を外す。最後の動きは上で書いてある)
			if (!isUsing) {
				CommitDrag(context);
			}
		}

		isOverGizmo_ = ImGuizmo::IsUsing() || ImGuizmo::GetHoveredHandleType() != ImGuizmo::MT_NONE;
	}


	void SceneGizmo::HandleShortcuts(const bool isHovered, const bool isCameraFlying)
	{
		// 右ドラッグ中の W / E は編集用カメラの移動。ドラッグ中は操作の種類を変えない
		if (!isHovered || isCameraFlying || isDragging_) return;
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeySuper) return;

		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
			operation_ = Operation::Translate;
		} else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
			operation_ = Operation::Rotate;
		} else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			operation_ = Operation::Scale;
		}
	}


	void SceneGizmo::Cancel(const EditorContext& context)
	{
		isOverGizmo_ = false;
		if (!isDragging_) return;

		// プレビューが作り直されていれば控えたエンティティは捨てられている(新しいエンティティへ古い値を書かない)
		const aq::level::LevelEditModel* model = context.GetModel();
		const bool samePreview = (model != nullptr) && model->GetPreviewLevelId() == dragPreviewId_;
		EndDrag(samePreview);
	}


	bool SceneGizmo::BeginDrag(const EditorContext& context, const uint32_t nodeId, const aq::ecs::EntityHandle& entity)
	{
		const aq::level::LevelEditModel* model = context.GetModel();
		if (model == nullptr) return false;
		const aq::ecs::PrefabEditNode* node = model->FindNode(nodeId);
		if (node == nullptr) return false;

		// モデルの Transform の有無はここで 1 回だけ調べる(参照ノードは参照先のファイルを読むので毎フレームは呼ばない)
		aq::util::JsonValue probe;
		isReferenceNode_   = !node->prefabRef.empty();
		modelHasTransform_ = model->GetComponentField(nodeId, TRANSFORM_TYPE_NAME, POSITION_KEY, probe);
		if (isReferenceNode_ && !modelHasTransform_) {
			// 解決した参照先に Transform が無い。override で足す経路は E12 で扱うので、このノードにはギズモを出さない
			hiddenNodeId_    = nodeId;
			hiddenPreviewId_ = model->GetPreviewLevelId();
			hiddenVersion_   = context.GetVersion();
			return false;
		}

		aq::ecs::EntityContext& ctx = aq::ecs::EntityContext::Get();
		const auto* transform    = ctx.GetComponent<aq::ecs::TransformComponent>(entity);
		const auto* hierarchical = ctx.GetComponent<aq::ecs::HierarchicalTransformComponent>(entity);
		if (transform == nullptr || hierarchical == nullptr) return false;

		// 親は UpdateWorld と同じ条件(有効で Transform と HierarchicalTransform を持つ)のときだけ使う。ドラッグ中は親は動かない
		hasParent_ = false;
		if (ctx.IsValid(hierarchical->parentHandle)) {
			const auto* parentHierarchical = ctx.GetComponent<aq::ecs::HierarchicalTransformComponent>(hierarchical->parentHandle);
			if (parentHierarchical != nullptr && ctx.GetComponent<aq::ecs::TransformComponent>(hierarchical->parentHandle) != nullptr) {
				hasParent_   = true;
				parentWorld_ = parentHierarchical->transform;
			}
		}

		originalLocal_.position = transform->position;
		originalLocal_.rotation = transform->rotation;
		originalLocal_.scale    = transform->scale;
		currentLocal_           = originalLocal_;

		isDragging_     = true;
		hasManipulated_ = false;
		dragNodeId_     = nodeId;
		dragEntity_     = entity;
		dragPreviewId_  = model->GetPreviewLevelId();
		dragVersion_    = context.GetVersion();
		return true;
	}


	void SceneGizmo::ApplyMatrix()
	{
		aq::ecs::Transform world;
		if (!GizmoMath::DecomposeMatrix(gizmoMatrix_, world)) return;
		aq::ecs::Transform local;
		if (!GizmoMath::WorldToLocal(world, hasParent_ ? &parentWorld_ : nullptr, local)) return;

		// 分解と逆算の誤差で、動かしていない成分のキーを変えない(元の値のまま残す)
		if (NearlyEqual(local.position, originalLocal_.position)) local.position = originalLocal_.position;
		if (NearlyEqual(local.rotation, originalLocal_.rotation)) local.rotation = originalLocal_.rotation;
		if (NearlyEqual(local.scale,    originalLocal_.scale))    local.scale    = originalLocal_.scale;

		currentLocal_ = local;
		WriteLocal(local);
	}


	void SceneGizmo::CommitDrag(EditorContext& context)
	{
		const bool positionChanged = !SameBits(currentLocal_.position, originalLocal_.position);
		const bool rotationChanged = !SameBits(currentLocal_.rotation, originalLocal_.rotation);
		const bool scaleChanged    = !SameBits(currentLocal_.scale,    originalLocal_.scale);
		const uint32_t nodeId = dragNodeId_;

		// 押しただけ・元に戻した: 何も積まない(エンティティは元の値のまま)
		isDragging_ = false;
		if (!hasManipulated_ || (!positionChanged && !rotationChanged && !scaleChanged)) return;

		// 積んだ後は次のフレームの先頭で実行され、プレビューが作り直される。それまでエンティティはドラッグ後の値のまま
		if (!isReferenceNode_ && !modelHasTransform_) {
			// モデルの実体ノードに Transform が無い(生成で自動で足されたので画面には出ている): ドラッグ後の値を持つ Transform を足す
			aq::util::JsonValue json = aq::util::JsonValue::MakeObject();
			json.Set(POSITION_KEY, aq::ecs::ToJson(currentLocal_.position));
			json.Set(ROTATION_KEY, aq::ecs::ToJson(currentLocal_.rotation));
			json.Set(SCALE_KEY,    aq::ecs::ToJson(currentLocal_.scale));
			context.Submit(std::make_unique<AddComponentCommand>(nodeId, TRANSFORM_TYPE_NAME, json));
			return;
		}

		// 変わったキーだけを SetField にする(参照ノードは override になる)。2 つ以上なら 1 つにまとめて 1 回で積む
		std::vector<std::unique_ptr<IEditorCommand>> commands;
		if (positionChanged) {
			commands.push_back(std::make_unique<SetFieldCommand>(nodeId, TRANSFORM_TYPE_NAME, POSITION_KEY, aq::ecs::ToJson(currentLocal_.position)));
		}
		if (rotationChanged) {
			commands.push_back(std::make_unique<SetFieldCommand>(nodeId, TRANSFORM_TYPE_NAME, ROTATION_KEY, aq::ecs::ToJson(currentLocal_.rotation)));
		}
		if (scaleChanged) {
			commands.push_back(std::make_unique<SetFieldCommand>(nodeId, TRANSFORM_TYPE_NAME, SCALE_KEY, aq::ecs::ToJson(currentLocal_.scale)));
		}

		if (commands.size() == 1) {
			context.Submit(std::move(commands.front()));
			return;
		}
		auto composite = std::make_unique<CompositeCommand>(COMPOSITE_COMMAND_NAME);
		for (std::unique_ptr<IEditorCommand>& command : commands) {
			composite->Add(std::move(command));
		}
		context.Submit(std::move(composite));
	}


	void SceneGizmo::EndDrag(const bool writeBack)
	{
		if (writeBack) {
			WriteLocal(originalLocal_);
		}
		isDragging_ = false;
		ResetImGuizmo();
	}


	void SceneGizmo::WriteLocal(const aq::ecs::Transform& local) const
	{
		aq::ecs::EntityContext& ctx = aq::ecs::EntityContext::Get();
		if (!ctx.IsValid(dragEntity_)) return;
		auto* transform = ctx.GetComponent<aq::ecs::TransformComponent>(dragEntity_);
		if (transform == nullptr) return;

		// 値の書き換えは構造の変更ではないので、ECS の反復の外(OnBuildUI)ならその場で書いてよい。ワールドは次の更新で UpdateWorld が求める
		transform->position = local.position;
		transform->rotation = local.rotation;
		transform->scale    = local.scale;
	}
}
