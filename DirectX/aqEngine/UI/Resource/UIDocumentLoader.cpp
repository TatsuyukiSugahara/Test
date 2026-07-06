#include "aq.h"
#include "UIDocumentLoader.h"
#include "UI/UIObject.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UICanvasComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UINineSliceComponent.h"
#include "UI/Component/UICircleGaugeComponent.h"
#include "UI/Component/UIButtonComponent.h"
#include "UI/Component/UITextComponent.h"
#include "UI/Component/UIAnimationComponent.h"
#include "UI/Animation/UIAnimationSerializer.h"
#include "Util/SimpleJson.h"
#include "Graphics/IShaderResourceView.h"

namespace aq
{
	namespace ui
	{
		// ---- DeferredSRV --------------------------------------------------------
		// GPUResource の非同期ロード完了後に SRV を解決するラッパー。
		// Release() は no-op (所有権は GPUResource 側の TextureData が持つ)。
		// GetNativeHandle() はロード完了後に実 SRV へ委譲する。

		namespace
		{
			class DeferredSRV final : public graphics::IShaderResourceView
			{
			public:
				explicit DeferredSRV(std::shared_ptr<res::GPUResource> res)
					: res_(std::move(res))
				{}

				void  Release() override {}

				void* GetNativeHandle() const override
				{
					if (!res_) return nullptr;
					const auto* srv = res_->GetShaderResourceView();
					return srv ? srv->GetNativeHandle() : nullptr;
				}

			private:
				std::shared_ptr<res::GPUResource> res_;
			};

			// テクスチャパスを非同期ロードし、DeferredSRV でラップして返す。
			std::shared_ptr<graphics::IShaderResourceView> LoadTex(const std::string& path)
			{
				if (path.empty()) return nullptr;
				auto gpuRes = res::ResourceManager::Get().Load<res::GPUResource>(path.c_str());
				if (!gpuRes) return nullptr;
				return std::make_shared<DeferredSRV>(std::move(gpuRes));
			}

		} // anonymous namespace


		// ---- JSON ヘルパー -------------------------------------------------------

		namespace
		{
			// [x, y] 配列を Vector2 に変換
			math::Vector2 ToVec2(const util::JsonValue& v, math::Vector2 def = {})
			{
				if (!v.IsArray() || v.Size() < 2) return def;
				return { v[0].AsFloat(def.x), v[1].AsFloat(def.y) };
			}

			// [x, y, z] 配列を Vector3 に変換
			math::Vector3 ToVec3(const util::JsonValue& v, math::Vector3 def = {})
			{
				if (!v.IsArray() || v.Size() < 3) return def;
				return { v[0].AsFloat(def.x), v[1].AsFloat(def.y), v[2].AsFloat(def.z) };
			}

			// [r, g, b, a] 配列を Vector4 に変換
			math::Vector4 ToVec4(const util::JsonValue& v, math::Vector4 def = { 1,1,1,1 })
			{
				if (!v.IsArray() || v.Size() < 4) return def;
				return { v[0].AsFloat(def.x), v[1].AsFloat(def.y),
				         v[2].AsFloat(def.z), v[3].AsFloat(def.w) };
			}

			FillDirection ToFillDir(const util::JsonValue& v)
			{
				const auto& s = v.AsString();
				if (s == "Left")  return FillDirection::Left;
				if (s == "Up")    return FillDirection::Up;
				if (s == "Down")  return FillDirection::Down;
				return FillDirection::Right;
			}

		} // anonymous namespace


		// ---- コンポーネント適用 -------------------------------------------------

		namespace
		{
			void ApplyTransform(UIObject& obj, const util::JsonValue& j)
			{
				auto* t = obj.HasComponent<UITransformComponent>()
				          ? obj.GetComponent<UITransformComponent>()
				          : obj.AddComponent<UITransformComponent>();

				if (!j["position"].IsNull())
					t->localPosition = ToVec3(j["position"]);
				if (!j["scale"].IsNull())
					t->localScale = ToVec2(j["scale"], { 1.f, 1.f });
				if (!j["rotation"].IsNull())
					t->rotation = j["rotation"].AsFloat();
				if (!j["sizeDelta"].IsNull())
					t->sizeDelta = ToVec2(j["sizeDelta"]);
				if (!j["active"].IsNull())
					t->active = j["active"].AsBool(true);

				if (!j["anchor"].IsNull())
				{
					const auto& anc = j["anchor"];
					if (!anc["min"].IsNull()) t->anchor.min = ToVec2(anc["min"]);
					if (!anc["max"].IsNull()) t->anchor.max = ToVec2(anc["max"]);
				}
				if (!j["pivot"].IsNull())
					t->pivot.pivot = ToVec2(j["pivot"], { 0.5f, 0.5f });
			}

			void ApplyCanvas(UIObject& obj, const util::JsonValue& j)
			{
				auto* c = obj.HasComponent<UICanvasComponent>()
				          ? obj.GetComponent<UICanvasComponent>()
				          : obj.AddComponent<UICanvasComponent>();

				if (!j["resolution"].IsNull())
					c->resolution = ToVec2(j["resolution"], { 1920.f, 1080.f });
				if (!j["sortOrder"].IsNull())
					c->sortOrder = j["sortOrder"].AsInt();
			}

			void ApplyImage(UIObject& obj, const util::JsonValue& j)
			{
				auto* img = obj.HasComponent<UIImageComponent>()
				            ? obj.GetComponent<UIImageComponent>()
				            : obj.AddComponent<UIImageComponent>();

				if (!j["texture"].IsNull())
					img->texture = LoadTex(j["texture"].AsString());
				if (!j["color"].IsNull())
					img->color = ToVec4(j["color"]);
				if (!j["uvRect"].IsNull())
				{
					const auto& u = j["uvRect"];
					if (u.Size() >= 4)
						img->uvRect = { u[0].AsFloat(), u[1].AsFloat(),
						                u[2].AsFloat(1.f), u[3].AsFloat(1.f) };
				}
				if (!j["fillAmount"].IsNull())
					img->fillAmount = j["fillAmount"].AsFloat(1.f);
				if (!j["fillDir"].IsNull())
					img->fillDir = ToFillDir(j["fillDir"]);
				if (!j["flipH"].IsNull())
					img->flipH = j["flipH"].AsBool();
				if (!j["flipV"].IsNull())
					img->flipV = j["flipV"].AsBool();
			}

			void ApplyNineSlice(UIObject& obj, const util::JsonValue& j)
			{
				auto* ns = obj.HasComponent<UINineSliceComponent>()
				           ? obj.GetComponent<UINineSliceComponent>()
				           : obj.AddComponent<UINineSliceComponent>();

				if (!j["texture"].IsNull())
					ns->texture = LoadTex(j["texture"].AsString());
				if (!j["color"].IsNull())
					ns->color = ToVec4(j["color"]);
				if (!j["border"].IsNull())
				{
					const auto& b = j["border"];
					if (!b["left"].IsNull())   ns->border.left   = b["left"].AsFloat();
					if (!b["right"].IsNull())  ns->border.right  = b["right"].AsFloat();
					if (!b["top"].IsNull())    ns->border.top    = b["top"].AsFloat();
					if (!b["bottom"].IsNull()) ns->border.bottom = b["bottom"].AsFloat();
				}
				if (!j["textureSize"].IsNull())
				{
					const auto& ts = j["textureSize"];
					if (ts.Size() >= 2)
						ns->textureSize = { ts[0].AsFloat(64.f), ts[1].AsFloat(64.f) };
				}
				if (!j["fillAmount"].IsNull())
					ns->fillAmount = j["fillAmount"].AsFloat(1.f);
				if (!j["fillDir"].IsNull())
					ns->fillDir = ToFillDir(j["fillDir"]);
			}

			void ApplyCircleGauge(UIObject& obj, const util::JsonValue& j)
			{
				auto* cg = obj.HasComponent<UICircleGaugeComponent>()
				           ? obj.GetComponent<UICircleGaugeComponent>()
				           : obj.AddComponent<UICircleGaugeComponent>();

				if (!j["texture"].IsNull())
					cg->texture = LoadTex(j["texture"].AsString());
				if (!j["color"].IsNull())
					cg->color = ToVec4(j["color"]);
				if (!j["fillAmount"].IsNull())
					cg->fillAmount = j["fillAmount"].AsFloat(1.f);
				if (!j["startAngle"].IsNull())
					cg->startAngle = j["startAngle"].AsFloat();
				if (!j["clockwise"].IsNull())
				{
					// bool: true=1.f(時計回り), false=-1.f(反時計回り)
					// number: 値をそのまま使用 (シェーダーは > 0 で時計回り判定)
					if (j["clockwise"].IsBool())
						cg->clockwise = j["clockwise"].AsBool(true) ? 1.f : -1.f;
					else
						cg->clockwise = j["clockwise"].AsFloat(1.f);
				}
			}

			void ApplyButton(UIObject& obj, const util::JsonValue& j)
			{
				auto* btn = obj.HasComponent<UIButtonComponent>()
				            ? obj.GetComponent<UIButtonComponent>()
				            : obj.AddComponent<UIButtonComponent>();

				if (!j["interactable"].IsNull())
					btn->interactable = j["interactable"].AsBool(true);
			}

			void ApplyText(UIObject& obj, const util::JsonValue& j)
			{
				auto* txt = obj.HasComponent<UITextComponent>()
				            ? obj.GetComponent<UITextComponent>()
				            : obj.AddComponent<UITextComponent>();

				if (!j["content"].IsNull())
					txt->content       = j["content"].AsString();
				if (!j["textStylePath"].IsNull())
					txt->textStylePath = j["textStylePath"].AsString();
				if (!j["fontSize"].IsNull())
					txt->fontSize      = j["fontSize"].AsFloat(0.f);
				if (!j["scale"].IsNull())
					txt->scale         = j["scale"].AsFloat(1.f);
				if (!j["color"].IsNull())
					txt->color         = ToVec4(j["color"], { 0.f, 0.f, 0.f, 0.f });
				if (!j["offset"].IsNull())
					txt->offset        = ToVec2(j["offset"], { 0.f, 0.f });
				if (!j["wordWrap"].IsNull())
					txt->wordWrap = j["wordWrap"].AsBool();

				if (!j["alignH"].IsNull())
				{
					const auto& s = j["alignH"].AsString();
					if (s == "Left")       txt->alignH = TextAlignH::Left;
					else if (s == "Right") txt->alignH = TextAlignH::Right;
					else                   txt->alignH = TextAlignH::Center;
				}
				if (!j["alignV"].IsNull())
				{
					const auto& s = j["alignV"].AsString();
					if (s == "Top")          txt->alignV = TextAlignV::Top;
					else if (s == "Bottom")  txt->alignV = TextAlignV::Bottom;
					else                     txt->alignV = TextAlignV::Middle;
				}
			}

			void ApplyComponents(UIObject& obj, const util::JsonValue& comps)
			{
				if (!comps["transform"].IsNull())   ApplyTransform  (obj, comps["transform"]);
				if (!comps["canvas"].IsNull())      ApplyCanvas     (obj, comps["canvas"]);
				if (!comps["image"].IsNull())       ApplyImage      (obj, comps["image"]);
				if (!comps["nineSlice"].IsNull())   ApplyNineSlice  (obj, comps["nineSlice"]);
				if (!comps["circleGauge"].IsNull()) ApplyCircleGauge(obj, comps["circleGauge"]);
				if (!comps["button"].IsNull())      ApplyButton     (obj, comps["button"]);
				if (!comps["text"].IsNull())        ApplyText       (obj, comps["text"]);
			}

		} // anonymous namespace


		// ---- ノード生成 (前方宣言) ----------------------------------------------

		static UIObject* InstantiateNode(
			const util::JsonValue& node,
			UIObject*              parent,
			UIContext&             ctx);


		// ---- ref/overrides 解決 -------------------------------------------------

		namespace
		{
			util::JsonValue ResolveRef(const util::JsonValue& node)
			{
				const auto& refVal = node["ref"];
				if (refVal.IsNull()) return node;

				// 参照先 JSON をロード
				util::JsonValue base = util::JsonParser::ParseFile(refVal.AsString().c_str());
				if (base.IsNull()) return node; // ファイルが存在しない場合はフォールバック

				// "overrides" を deep merge
				const auto& overrides = node["overrides"];
				if (!overrides.IsNull())
					base.Merge(overrides);

				return base;
			}

		} // anonymous namespace


		// ---- ノード生成本体 -----------------------------------------------------

		static UIObject* InstantiateNode(
			const util::JsonValue& node,
			UIObject*              parent,
			UIContext&             ctx)
		{
			// "ref" フィールドがあれば参照先を展開して上書き
			util::JsonValue resolved = ResolveRef(node);

			// 名前
			const std::string name = resolved["name"].AsString();
			UIObject* obj = ctx.CreateObject(name.empty() ? "UIObject" : name);

			// 親子関係
			if (parent) parent->AddChild(obj);

			// コンポーネント適用
			const auto& comps = resolved["components"];
			if (!comps.IsNull())
				ApplyComponents(*obj, comps);

			// アニメーションクリップ読み込み
			const auto& animJson = resolved["animation"];
			if (!animJson.IsNull())
			{
				auto* anim = obj->HasComponent<UIAnimationComponent>()
				             ? obj->GetComponent<UIAnimationComponent>()
				             : obj->AddComponent<UIAnimationComponent>();
				UIAnimationSerializer::LoadAll(animJson, *anim);
			}

			// 子を再帰生成
			const auto& children = resolved["children"];
			if (children.IsArray())
			{
				for (size_t i = 0; i < children.Size(); ++i)
					InstantiateNode(children[i], obj, ctx);
			}

			return obj;
		}


		// ---- 公開 API -----------------------------------------------------------

		UIObject* UIDocumentLoader::Load(
			std::string_view screenName,
			std::string_view docPath,
			UIContext&        ctx)
		{
			if (docPath.empty())
				return ctx.CreateObject(screenName.empty() ? "UIObject" : screenName);

			const std::string pathStr(docPath);
			util::JsonValue rootJson = util::JsonParser::ParseFile(pathStr.c_str());
			if (rootJson.IsNull())
				return ctx.CreateObject(screenName.empty() ? "UIObject" : screenName);

			// ルートに "name" がなければスクリーン名をフォールバック
			if (rootJson["name"].IsNull() || rootJson["name"].AsString().empty())
				rootJson.Set("name", util::JsonValue(std::string(screenName)));

			return InstantiateNode(rootJson, nullptr, ctx);
		}

	} // namespace ui
} // namespace aq
