#pragma once
#include <string>
#include "ECS/ECS.h"
#include "Math/Vector.h"
#include "Resource/Resource.h"
#include "Component/TransformComponentSystem.h"
#include "Rendering/RenderFrame.h"


namespace aq
{
	namespace ecs
	{
		/**
		 * 投影デカールコンポーネント。
		 *
		 * エンティティの (HierarchicalTransform) 位置・回転・スケールを中心とする
		 * 単位ボックス ([-0.5,0.5]^3 × size) を GBuffer へ投影し、その範囲の
		 * 不透明サーフェスにデカールテクスチャを貼り付ける。
		 *
		 * 投影軸はローカル -Y（無回転なら真下＝地面に水平に貼る）。
		 * size_.x / size_.z が貼り付け範囲、size_.y がボックスの厚み（投影深度）。
		 * 壁に貼る場合は X 軸 90° 回転などで投影軸を壁法線へ向ける。
		 */
		class DecalComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::DecalComponent);

		private:
			enum class State : uint8_t
			{
				Invalid,      // テクスチャ未指定
				LoadRequest,
				Loading,
				Ready,
			};

		private:
			State                   state_        = State::Invalid;
			std::string             texturePath_;
			aq::res::RefGPUResource texture_;
			bool                    loadRequested_ = false;

			// パラメータ
			aq::math::Vector3 size_         = aq::math::Vector3(2.0f, 2.0f, 2.0f); // x,z=範囲, y=投影深度(厚み)
			aq::math::Vector3 color_        = aq::math::Vector3(1.0f, 1.0f, 1.0f); // 色ティント
			float             opacity_      = 1.0f;                                // 不透明度
			float             angleFadeMin_ = 0.1f;                                // 角度フェード下限 (cosθ)

		public:
			DecalComponent() = default;
			~DecalComponent() = default;

			void SetTexturePath(const char* path);
			void SetSize(const aq::math::Vector3& size)   { size_ = size; }
			void SetColor(const aq::math::Vector3& color) { color_ = color; }
			void SetOpacity(float opacity)                { opacity_ = opacity; }
			void SetAngleFadeMin(float cosMin)            { angleFadeMin_ = cosMin; }

			void Update();
			bool IsReady() const { return state_ == State::Ready; }

			const char* StateName() const
			{
				switch (state_)
				{
					case State::Invalid:     return "Invalid (テクスチャ未指定 or ロード失敗)";
					case State::LoadRequest: return "LoadRequest";
					case State::Loading:     return "Loading";
					case State::Ready:       return "Ready";
				}
				return "?";
			}

			/** ワールド変換 xf を使ってデカール描画アイテムを構築する。未準備なら false。 */
			bool FillDecalItem(aq::rendering::DecalRenderItem& out, const aq::ecs::Transform& xf) const;

			// 永続フィールドの列挙（ImGui 編集 / JSON 保存 / JSON 読込で共有）。
			// FieldPath が true（= 値更新）を返すと SetTexturePath が走り、
			// JSON 読込時にもテクスチャのロードが発火する。
			template <typename V>
			void Reflect(V& visitor)
			{
				std::string newPath = texturePath_;
				if (visitor.FieldPath("texture", newPath, "Texture"))
					SetTexturePath(newPath.empty() ? nullptr : newPath.c_str());
				visitor.Field("size",         size_,         "Size (xz=範囲 y=深度)");
				visitor.Field("color",        color_,        "Color");
				visitor.Field("opacity",      opacity_,      "Opacity");
				visitor.Field("angleFadeMin", angleFadeMin_, "AngleFadeMin");
				visitor.ReadOnly("state", StateName());
			}
		};
	}
}
