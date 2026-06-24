#include "aq.h"
#include "TextStyle.h"
#include "Util/SimpleJson.h"

namespace aq
{
	namespace ui
	{
		namespace
		{
			using JV = util::JsonValue;

			math::Vector4 ToVec4(const JV& v, math::Vector4 def = { 1,1,1,1 })
			{
				if (!v.IsArray() || v.Size() < 4) return def;
				return { v[0].AsFloat(def.x), v[1].AsFloat(def.y),
				         v[2].AsFloat(def.z), v[3].AsFloat(def.w) };
			}

			math::Vector2 ToVec2(const JV& v, math::Vector2 def = {})
			{
				if (!v.IsArray() || v.Size() < 2) return def;
				return { v[0].AsFloat(def.x), v[1].AsFloat(def.y) };
			}

			JV FromVec4(const math::Vector4& v)
			{
				JV arr = JV::MakeArray();
				arr.PushBack(JV((double)v.x));
				arr.PushBack(JV((double)v.y));
				arr.PushBack(JV((double)v.z));
				arr.PushBack(JV((double)v.w));
				return arr;
			}

			JV FromVec2(const math::Vector2& v)
			{
				JV arr = JV::MakeArray();
				arr.PushBack(JV((double)v.x));
				arr.PushBack(JV((double)v.y));
				return arr;
			}

		} // anonymous namespace


		bool TextStyle::LoadFromJson(const char* path)
		{
			JV root = util::JsonParser::ParseFile(path);
			if (root.IsNull()) return false;

			if (!root["name"].IsNull())      name      = root["name"].AsString();
			if (!root["fontPath"].IsNull())  fontPath  = root["fontPath"].AsString();
			if (!root["fontSize"].IsNull())  fontSize  = root["fontSize"].AsFloat(24.f);
			if (!root["fillColor"].IsNull()) fillColor = ToVec4(root["fillColor"]);

			const JV& outJ = root["outline"];
			if (!outJ.IsNull())
			{
				if (!outJ["enabled"].IsNull()) outline.enabled = outJ["enabled"].AsBool();
				if (!outJ["color"].IsNull())   outline.color   = ToVec4(outJ["color"], { 0,0,0,1 });
				if (!outJ["width"].IsNull())   outline.width   = outJ["width"].AsFloat(0.08f);
			}

			const JV& shJ = root["shadow"];
			if (!shJ.IsNull())
			{
				if (!shJ["enabled"].IsNull())  shadow.enabled  = shJ["enabled"].AsBool();
				if (!shJ["color"].IsNull())    shadow.color    = ToVec4(shJ["color"], { 0,0,0,0.6f });
				if (!shJ["offset"].IsNull())   shadow.offset   = ToVec2(shJ["offset"], { 2.f,-2.f });
				if (!shJ["softness"].IsNull()) shadow.softness = shJ["softness"].AsFloat(0.05f);
			}

			const JV& grJ = root["gradient"];
			if (!grJ.IsNull())
			{
				if (!grJ["enabled"].IsNull())     gradient.enabled     = grJ["enabled"].AsBool();
				if (!grJ["topColor"].IsNull())    gradient.topColor    = ToVec4(grJ["topColor"]);
				if (!grJ["bottomColor"].IsNull()) gradient.bottomColor = ToVec4(grJ["bottomColor"]);
			}

			return true;
		}


		bool TextStyle::SaveToJson(const char* path) const
		{
			JV root = JV::MakeObject();
			root.Set("name",      JV(name));
			root.Set("fontPath",  JV(fontPath));
			root.Set("fontSize",  JV((double)fontSize));
			root.Set("fillColor", FromVec4(fillColor));

			JV outJ = JV::MakeObject();
			outJ.Set("enabled", JV(outline.enabled));
			outJ.Set("color",   FromVec4(outline.color));
			outJ.Set("width",   JV((double)outline.width));
			root.Set("outline", std::move(outJ));

			JV shJ = JV::MakeObject();
			shJ.Set("enabled",  JV(shadow.enabled));
			shJ.Set("color",    FromVec4(shadow.color));
			shJ.Set("offset",   FromVec2(shadow.offset));
			shJ.Set("softness", JV((double)shadow.softness));
			root.Set("shadow", std::move(shJ));

			JV grJ = JV::MakeObject();
			grJ.Set("enabled",     JV(gradient.enabled));
			grJ.Set("topColor",    FromVec4(gradient.topColor));
			grJ.Set("bottomColor", FromVec4(gradient.bottomColor));
			root.Set("gradient", std::move(grJ));

			return util::JsonSerializer::WriteFile(path, root);
		}


		TextStyle TextStyle::MakeDefault()
		{
			TextStyle s;
			s.name     = "Default";
			s.fontSize = 24.f;
			s.fillColor = { 1.f, 1.f, 1.f, 1.f };
			return s;
		}

	} // namespace ui
} // namespace aq
