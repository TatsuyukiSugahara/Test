#pragma once
#include <cmath>


namespace editor
{
	/**
	 * ギズモの行列の組み立て・分解と、ワールド ↔ ローカルの変換(設計書/エディタ/E11_シーンビュー.md 7.)。
	 * エンジンの行列は左手系・行ベクトル・行優先(XMFLOAT4X4 と同じ並び。平行移動は _41〜_43)。
	 * ImGuizmo も行ベクトル・平行移動が [12]〜[14] の並びで計算するので、この行列をそのまま float[16] として渡す(並びの変換は要らない)。
	 * ワールドの合成は行列の積ではなく成分ごと(HierarcicalTransformSystem::UpdateWorld と同じ規則)なので、逆算も成分ごとに行う
	 */
	class GizmoMath final
	{
	public:
		/** 拡縮の下限(TransformComponent の scale の FieldAttr の Min と同じ) */
		static constexpr float MIN_SCALE = 0.001f;

		/** 親の拡縮がこれより小さい成分は割れないとみなす */
		static constexpr float MIN_PARENT_SCALE = 1e-6f;


	public:
		/** 位置・回転・拡縮から S·R·T の行列を組む(描画のワールド行列と同じ順) */
		static aq::math::Matrix4x4 ComposeMatrix(const aq::ecs::Transform& transform)
		{
			aq::math::Matrix4x4 scale;       scale.MakeScaling(transform.scale);
			aq::math::Matrix4x4 rotation;    rotation.MakeRotationFromQuaternion(transform.rotation);
			aq::math::Matrix4x4 translation; translation.MakeTranslation(transform.position);
			aq::math::Matrix4x4 scaleRotation; scaleRotation.Mull(scale, rotation);
			aq::math::Matrix4x4 result;        result.Mull(scaleRotation, translation);
			return result;
		}


		/** S·R·T の行列を位置・回転・拡縮に分ける。分けられない(拡縮が 0 の軸がある等)なら false で out は変えない */
		static bool DecomposeMatrix(const aq::math::Matrix4x4& matrix, aq::ecs::Transform& out)
		{
			DirectX::XMVECTOR scale;
			DirectX::XMVECTOR rotation;
			DirectX::XMVECTOR translation;
			if (!DirectX::XMMatrixDecompose(&scale, &rotation, &translation, matrix)) return false;

			DirectX::XMStoreFloat3(&out.scale.vector, scale);
			DirectX::XMStoreFloat4(&out.rotation.vector, DirectX::XMQuaternionNormalize(rotation));
			DirectX::XMStoreFloat3(&out.position.vector, translation);
			return true;
		}


		/**
		 * ローカル → ワールド。HierarcicalTransformSystem::UpdateWorld と同じ規則(parentWorld が nullptr なら親なしでワールド = ローカル):
		 * 位置 = ローカルの位置 × 親のワールド行列(S·R·T)、回転 = ローカル × 親、拡縮 = 成分ごとの積
		 */
		static aq::ecs::Transform ComposeWorld(const aq::ecs::Transform& local, const aq::ecs::Transform* parentWorld)
		{
			if (parentWorld == nullptr) return local;

			aq::ecs::Transform world;
			const aq::math::Matrix4x4 parentMatrix = ComposeMatrix(*parentWorld);
			DirectX::XMStoreFloat3(&world.position.vector,
				DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&local.position.vector), parentMatrix));
			world.scale    = local.scale * parentWorld->scale;
			world.rotation = local.rotation * parentWorld->rotation;
			return world;
		}


		/**
		 * ワールド → ローカル(ComposeWorld の逆。parentWorld が nullptr ならローカル = ワールド)。
		 * 位置 = ワールドの位置 × 親のワールド行列(S·R·T)の逆行列、回転 = ワールド × 親の回転の逆、拡縮 = ワールド ÷ 親(成分ごと)。
		 * 成分ごとの規則なので、親の拡縮が非一様でも厳密に戻る(せん断は生じない)。
		 * 拡縮は MIN_SCALE で下を切る。親の拡縮に 0 に近い成分があれば false で outLocal は変えない
		 */
		static bool WorldToLocal(const aq::ecs::Transform& world, const aq::ecs::Transform* parentWorld, aq::ecs::Transform& outLocal)
		{
			aq::ecs::Transform local = world;
			if (parentWorld != nullptr) {
				const aq::math::Vector3& parentScale = parentWorld->scale;
				if (std::fabs(parentScale.x) < MIN_PARENT_SCALE || std::fabs(parentScale.y) < MIN_PARENT_SCALE
				 || std::fabs(parentScale.z) < MIN_PARENT_SCALE) {
					return false;
				}

				aq::math::Matrix4x4 parentInverse;
				parentInverse.Inverse(ComposeMatrix(*parentWorld));
				DirectX::XMStoreFloat3(&local.position.vector,
					DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&world.position.vector), parentInverse));

				// UpdateWorld の回転は XMQuaternionMultiply(ローカル, 親)。右から親の逆を掛けると戻る
				DirectX::XMStoreFloat4(&local.rotation.vector,
					DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(
						DirectX::XMLoadFloat4(&world.rotation.vector),
						DirectX::XMQuaternionInverse(DirectX::XMLoadFloat4(&parentWorld->rotation.vector)))));

				local.scale = aq::math::Vector3(world.scale.x / parentScale.x, world.scale.y / parentScale.y, world.scale.z / parentScale.z);
			}

			local.scale = aq::math::Vector3(std::fmax(local.scale.x, MIN_SCALE), std::fmax(local.scale.y, MIN_SCALE), std::fmax(local.scale.z, MIN_SCALE));
			outLocal = local;
			return true;
		}
	};
}
