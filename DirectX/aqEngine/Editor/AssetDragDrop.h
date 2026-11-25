#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "ECS/FieldAttr.h"
#include <imgui/imgui.h>
#include <cstring>
#include <string>


namespace aq
{
	namespace editor
	{
		/** アセットのドラッグの荷物の種類名(エディタ全体設計 7.10 節 2.) */
		static constexpr const char* ASSET_DRAG_PAYLOAD = "AQ_ASSET";




		/**
		 * アセットのドラッグの荷物。
		 * ImGui が写しを持つので固定長にする。path は Assets/... の '/' 区切りで、NUL で終わる
		 */
		struct AssetDragPayload
		{
			/** 荷物のパスの容量(NUL を含む)。fieldwidget::DrawString の入力の長さと同じ */
			static constexpr size_t PATH_CAPACITY = 512;

			ecs::AssetKind kind = ecs::AssetKind::None;
			char           path[PATH_CAPACITY] = {};
		};




		/**
		 * 荷物を置く。BeginDragDropSource の中で呼ぶ。
		 * 種類が None、またはパスが容量に入らない(NUL を除いて PATH_CAPACITY - 1 バイト超)なら置かずに false
		 */
		inline bool SetAssetDragPayload(const ecs::AssetKind kind, const std::string& path)
		{
			if (kind == ecs::AssetKind::None) return false;
			if (path.empty() || path.size() >= AssetDragPayload::PATH_CAPACITY) return false;

			AssetDragPayload payload;
			payload.kind = kind;
			std::memcpy(payload.path, path.c_str(), path.size() + 1);
			ImGui::SetDragDropPayload(ASSET_DRAG_PAYLOAD, &payload, sizeof(payload));
			return true;
		}


		/**
		 * 今ドラッグ中の荷物が accept の種類のアセットか(受けずに覗くだけ)。accept が None なら常に false
		 */
		inline bool PeekAssetDragPayload(const ecs::AssetKind accept)
		{
			if (accept == ecs::AssetKind::None) return false;

			const ImGuiPayload* peek = ImGui::GetDragDropPayload();
			if (peek == nullptr || !peek->IsDataType(ASSET_DRAG_PAYLOAD)) return false;
			if (peek->DataSize != static_cast<int>(sizeof(AssetDragPayload))) return false;

			const AssetDragPayload* payload = static_cast<const AssetDragPayload*>(peek->Data);
			return payload->kind == accept;
		}


		/**
		 * 荷物を受ける。BeginDragDropTarget の中で呼ぶ。
		 * 種類が accept と違う荷物には AcceptDragDropPayload を呼ばない(受けられない所を光らせない)。
		 * ドロップされたフレームだけ outPath にパスを入れて true
		 */
		inline bool AcceptAssetDragPayload(const ecs::AssetKind accept, std::string& outPath)
		{
			if (!PeekAssetDragPayload(accept)) return false;

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_DRAG_PAYLOAD);
			if (payload == nullptr) return false;

			const AssetDragPayload* data = static_cast<const AssetDragPayload*>(payload->Data);
			// 念のため容量の中で NUL を探して終わりにする
			outPath.assign(data->path, strnlen(data->path, AssetDragPayload::PATH_CAPACITY));
			return true;
		}
	}
}
#endif
