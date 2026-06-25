#include "aq.h"
#include "UIDocumentSerializer.h"
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

namespace aq
{
	namespace ui
	{
		using JV = util::JsonValue;

		// ---- ヘルパー -----------------------------------------------------------

		namespace
		{
			JV FromVec2(const math::Vector2& v)
			{
				JV arr = JV::MakeArray();
				arr.PushBack(JV(static_cast<double>(v.x)));
				arr.PushBack(JV(static_cast<double>(v.y)));
				return arr;
			}

			JV FromVec3(const math::Vector3& v)
			{
				JV arr = JV::MakeArray();
				arr.PushBack(JV(static_cast<double>(v.x)));
				arr.PushBack(JV(static_cast<double>(v.y)));
				arr.PushBack(JV(static_cast<double>(v.z)));
				return arr;
			}

			JV FromVec4(const math::Vector4& v)
			{
				JV arr = JV::MakeArray();
				arr.PushBack(JV(static_cast<double>(v.x)));
				arr.PushBack(JV(static_cast<double>(v.y)));
				arr.PushBack(JV(static_cast<double>(v.z)));
				arr.PushBack(JV(static_cast<double>(v.w)));
				return arr;
			}

			const char* FillDirToString(FillDirection d)
			{
				switch (d)
				{
					case FillDirection::Left:  return "Left";
					case FillDirection::Up:    return "Up";
					case FillDirection::Down:  return "Down";
					default:                   return "Right";
				}
			}

			const char* AlignHToString(TextAlignH a)
			{
				switch (a)
				{
					case TextAlignH::Left:  return "Left";
					case TextAlignH::Right: return "Right";
					default:                return "Center";
				}
			}

			const char* AlignVToString(TextAlignV a)
			{
				switch (a)
				{
					case TextAlignV::Top:    return "Top";
					case TextAlignV::Bottom: return "Bottom";
					default:                 return "Middle";
				}
			}

		} // anonymous namespace


		// ---- コンポーネント シリアライズ ----------------------------------------

		namespace
		{
			JV SerializeTransform(const UITransformComponent* t)
			{
				JV j = JV::MakeObject();
				j.Set("position", FromVec3(t->localPosition));
				j.Set("scale",    FromVec2(t->localScale));
				j.Set("rotation", JV(static_cast<double>(t->rotation)));
				j.Set("sizeDelta", FromVec2(t->sizeDelta));
				j.Set("active",   JV(t->active));

				JV anc = JV::MakeObject();
				anc.Set("min", FromVec2(t->anchor.min));
				anc.Set("max", FromVec2(t->anchor.max));
				j.Set("anchor", std::move(anc));

				j.Set("pivot", FromVec2(t->pivot.pivot));
				return j;
			}

			JV SerializeCanvas(const UICanvasComponent* c)
			{
				JV j = JV::MakeObject();
				j.Set("resolution", FromVec2(c->resolution));
				j.Set("sortOrder",  JV(static_cast<double>(c->sortOrder)));
				return j;
			}

			JV SerializeImage(
				const UIImageComponent* img,
				UIObjectID id,
				const std::unordered_map<UIObjectID, std::string>& texPaths)
			{
				JV j = JV::MakeObject();

				auto it = texPaths.find(id);
				if (it != texPaths.end())
					j.Set("texture", JV(it->second));

				j.Set("color",      FromVec4(img->color));

				JV uvRect = JV::MakeArray();
				uvRect.PushBack(JV(static_cast<double>(img->uvRect.x)));
				uvRect.PushBack(JV(static_cast<double>(img->uvRect.y)));
				uvRect.PushBack(JV(static_cast<double>(img->uvRect.w)));
				uvRect.PushBack(JV(static_cast<double>(img->uvRect.h)));
				j.Set("uvRect",     std::move(uvRect));

				j.Set("fillAmount", JV(static_cast<double>(img->fillAmount)));
				j.Set("fillDir",    JV(std::string(FillDirToString(img->fillDir))));
				j.Set("flipH",      JV(img->flipH));
				j.Set("flipV",      JV(img->flipV));
				return j;
			}

			JV SerializeNineSlice(
				const UINineSliceComponent* ns,
				UIObjectID id,
				const std::unordered_map<UIObjectID, std::string>& texPaths)
			{
				JV j = JV::MakeObject();

				auto it = texPaths.find(id);
				if (it != texPaths.end())
					j.Set("texture", JV(it->second));

				j.Set("color",       FromVec4(ns->color));
				j.Set("textureSize", FromVec2(ns->textureSize));

				JV border = JV::MakeObject();
				border.Set("left",   JV(static_cast<double>(ns->border.left)));
				border.Set("right",  JV(static_cast<double>(ns->border.right)));
				border.Set("top",    JV(static_cast<double>(ns->border.top)));
				border.Set("bottom", JV(static_cast<double>(ns->border.bottom)));
				j.Set("border",     std::move(border));

				j.Set("fillAmount", JV(static_cast<double>(ns->fillAmount)));
				j.Set("fillDir",    JV(std::string(FillDirToString(ns->fillDir))));
				return j;
			}

			JV SerializeCircleGauge(
				const UICircleGaugeComponent* cg,
				UIObjectID id,
				const std::unordered_map<UIObjectID, std::string>& texPaths)
			{
				JV j = JV::MakeObject();

				auto it = texPaths.find(id);
				if (it != texPaths.end())
					j.Set("texture", JV(it->second));

				j.Set("color",      FromVec4(cg->color));
				j.Set("fillAmount", JV(static_cast<double>(cg->fillAmount)));
				j.Set("startAngle", JV(static_cast<double>(cg->startAngle)));
				j.Set("clockwise",  JV(cg->clockwise > 0.f));
				return j;
			}

			JV SerializeButton(const UIButtonComponent* btn)
			{
				JV j = JV::MakeObject();
				j.Set("interactable", JV(btn->interactable));
				return j;
			}

			JV SerializeText(const UITextComponent* txt)
			{
				JV j = JV::MakeObject();
				j.Set("content",       JV(txt->content));
				j.Set("textStylePath", JV(txt->textStylePath));
				j.Set("fontSize",      JV(static_cast<double>(txt->fontSize)));
				j.Set("scale",         JV(static_cast<double>(txt->scale)));
				j.Set("color",         FromVec4(txt->color));
				j.Set("offset",        FromVec2(txt->offset));
				j.Set("alignH",        JV(std::string(AlignHToString(txt->alignH))));
				j.Set("alignV",        JV(std::string(AlignVToString(txt->alignV))));
				j.Set("wordWrap",      JV(txt->wordWrap));
				return j;
			}

		} // anonymous namespace


		// ---- ノード再帰シリアライズ ---------------------------------------------

		namespace
		{
			JV SerializeNode(
				const UIObject* obj,
				const std::unordered_map<UIObjectID, std::string>& texPaths)
			{
				JV node = JV::MakeObject();
				node.Set("name", JV(std::string(obj->GetName())));

				JV comps = JV::MakeObject();
				const UIObjectID id = obj->GetHandle().id;

				if (auto* t = obj->GetComponent<UITransformComponent>())
					comps.Set("transform",  SerializeTransform(t));
				if (auto* c = obj->GetComponent<UICanvasComponent>())
					comps.Set("canvas",     SerializeCanvas(c));
				if (auto* img = obj->GetComponent<UIImageComponent>())
					comps.Set("image",      SerializeImage(img, id, texPaths));
				if (auto* ns = obj->GetComponent<UINineSliceComponent>())
					comps.Set("nineSlice",  SerializeNineSlice(ns, id, texPaths));
				if (auto* cg = obj->GetComponent<UICircleGaugeComponent>())
					comps.Set("circleGauge", SerializeCircleGauge(cg, id, texPaths));
				if (auto* btn = obj->GetComponent<UIButtonComponent>())
					comps.Set("button",     SerializeButton(btn));
				if (auto* txt = obj->GetComponent<UITextComponent>())
					comps.Set("text",       SerializeText(txt));

				if (!comps.GetObject().empty())
					node.Set("components", std::move(comps));

				if (auto* anim = obj->GetComponent<UIAnimationComponent>())
				{
					if (!anim->clips.empty())
						node.Set("animation", UIAnimationSerializer::SaveAll(*anim));
				}

				const auto& children = obj->GetChildren();
				if (!children.empty())
				{
					JV childArr = JV::MakeArray();
					for (const UIObject* child : children)
						childArr.PushBack(SerializeNode(child, texPaths));
					node.Set("children", std::move(childArr));
				}

				return node;
			}

		} // anonymous namespace


		// ---- 公開 API -----------------------------------------------------------

		bool UIDocumentSerializer::Save(
			const UIObject*                                   root,
			std::string_view                                  filePath,
			const std::unordered_map<UIObjectID, std::string>& texturePaths)
		{
			if (!root || filePath.empty()) return false;

			const JV rootJson = SerializeNode(root, texturePaths);
			return util::JsonSerializer::WriteFile(std::string(filePath).c_str(), rootJson);
		}

	} // namespace ui
} // namespace aq
