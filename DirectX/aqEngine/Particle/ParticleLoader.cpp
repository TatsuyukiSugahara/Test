#include "stdafx.h"
#include "ParticleLoader.h"
#include "Particle/ParticleTypes.h"
#include "Particle/ParticleSystemData.h"
#include "Util/SimpleJson.h"
#include <algorithm>
#include <cmath>


namespace aq
{
	namespace particle
	{
		namespace
		{
			using aq::util::JsonValue;

			/** ステップキー判定しきい値 (仕様 §4.2: |tangent| >= 1e18 をステップ扱い) */
			static constexpr float STEP_TANGENT_THRESHOLD = 1.0e18f;


			/** カーブキー (焼き込み前の中間表現) */
			struct CurveKey
			{
				float time;
				float value;
				float inTangent;
				float outTangent;
			};


			math::Vector3 ParseVec3(const JsonValue& j, const math::Vector3& def)
			{
				if (!j.IsArray() || j.Size() < 3) {
					return def;
				}
				return math::Vector3(j[0].AsFloat(def.x), j[1].AsFloat(def.y), j[2].AsFloat(def.z));
			}


			math::Vector4 ParseVec4(const JsonValue& j, const math::Vector4& def)
			{
				if (!j.IsArray() || j.Size() < 4) {
					return def;
				}
				return math::Vector4(j[0].AsFloat(def.x), j[1].AsFloat(def.y),
				                     j[2].AsFloat(def.z), j[3].AsFloat(def.w));
			}


			/** "keys" 配列を CurveKey 列へ。time 昇順にソートする (仕様 §4.2)。 */
			std::vector<CurveKey> ParseCurveKeys(const JsonValue& curve)
			{
				std::vector<CurveKey> keys;
				const JsonValue& arr = curve["keys"];
				if (!arr.IsArray()) {
					return keys;
				}
				keys.reserve(arr.Size());
				for (size_t i = 0; i < arr.Size(); ++i) {
					const JsonValue& k = arr[i];
					CurveKey key;
					key.time       = k["time"].AsFloat(0.0f);
					key.value      = k["value"].AsFloat(0.0f);
					key.inTangent  = k["inTangent"].AsFloat(0.0f);
					key.outTangent = k["outTangent"].AsFloat(0.0f);
					keys.push_back(key);
				}
				std::sort(keys.begin(), keys.end(),
					[](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
				return keys;
			}


			/** ソート済みキー列を s∈[0,1] で 3次 Hermite 評価する (仕様 §4.2)。 */
			float EvaluateHermite(const std::vector<CurveKey>& keys, const float s)
			{
				if (keys.empty()) {
					return 0.0f;
				}
				if (s <= keys.front().time) {
					return keys.front().value;
				}
				if (s >= keys.back().time) {
					return keys.back().value;
				}

				// s を含む区間を線形探索 (キー数は少数)
				size_t i = 0;
				while (i + 1 < keys.size() && keys[i + 1].time <= s) {
					++i;
				}
				const CurveKey& k0 = keys[i];
				const CurveKey& k1 = keys[i + 1];
				float dt = k1.time - k0.time;
				if (dt <= 0.0f) {
					return k0.value;
				}

				// Infinity/ステップタンジェントは前キー値を保持
				if (std::fabs(k0.outTangent) >= STEP_TANGENT_THRESHOLD ||
				    std::fabs(k1.inTangent)  >= STEP_TANGENT_THRESHOLD) {
					return k0.value;
				}

				float u   = (s - k0.time) / dt;
				float u2  = u * u;
				float u3  = u2 * u;
				float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
				float h10 = u3 - 2.0f * u2 + u;
				float h01 = -2.0f * u3 + 3.0f * u2;
				float h11 = u3 - u2;
				return h00 * k0.value + h10 * dt * k0.outTangent
				     + h01 * k1.value + h11 * dt * k1.inTangent;
			}


			/**
			 * カーブを LUT_SAMPLE_COUNT サンプルへ焼き込み pool へ連結。先頭オフセットを返す。
			 * multiplier も焼き込む (仕様 §4.2)。
			 */
			uint32_t BakeCurve(const std::vector<CurveKey>& keys, const float multiplier,
			                   std::vector<float>& pool)
			{
				uint32_t offset = static_cast<uint32_t>(pool.size());
				for (uint32_t i = 0; i < LUT_SAMPLE_COUNT; ++i) {
					float s = static_cast<float>(i) / static_cast<float>(LUT_SAMPLE_COUNT - 1u);
					pool.push_back(EvaluateHermite(keys, s) * multiplier);
				}
				return offset;
			}


			/** ScalarValue をパースし、必要なら pool へ LUT を焼き込む (仕様 §4.1)。 */
			ScalarValue ParseScalar(const JsonValue& j, std::vector<float>& pool, const float defConst)
			{
				ScalarValue out;
				if (!j.IsObject()) {
					out.mode = ScalarValue::Mode::Constant;
					out.a    = defConst;
					return out;
				}

				const std::string& mode = j["mode"].AsString();
				if (mode == "TwoConstants") {
					out.mode = ScalarValue::Mode::TwoConstants;
					out.a    = j["min"].AsFloat(defConst);
					out.b    = j["max"].AsFloat(defConst);
				} else if (mode == "Curve") {
					float multiplier   = j["multiplier"].AsFloat(1.0f);
					out.mode           = ScalarValue::Mode::Curve;
					out.lutMinOffset   = BakeCurve(ParseCurveKeys(j["curve"]), multiplier, pool);
				} else if (mode == "TwoCurves") {
					float multiplier   = j["multiplier"].AsFloat(1.0f);
					out.mode           = ScalarValue::Mode::TwoCurves;
					out.lutMinOffset   = BakeCurve(ParseCurveKeys(j["curveMin"]), multiplier, pool);
					out.lutMaxOffset   = BakeCurve(ParseCurveKeys(j["curveMax"]), multiplier, pool);
				} else {
					// "Constant" および未知モード
					out.mode = ScalarValue::Mode::Constant;
					out.a    = j["constant"].AsFloat(defConst);
				}
				return out;
			}


			/** ColorValue をパース (仕様 §4.3)。 */
			ColorValue ParseColorValue(const JsonValue& j, const math::Vector4& def)
			{
				ColorValue out;
				out.a = def;
				out.b = def;
				if (!j.IsObject()) {
					return out;
				}
				const std::string& mode = j["mode"].AsString();
				if (mode == "TwoConstants") {
					out.mode = ColorValue::Mode::TwoConstants;
					out.a    = ParseVec4(j["min"], def);
					out.b    = ParseVec4(j["max"], def);
				} else {
					out.mode = ColorValue::Mode::Constant;
					out.a    = ParseVec4(j["color"], def);
					out.b    = out.a;
				}
				return out;
			}


			/** 時刻昇順の (time,value) 列を t で線形補間しクランプ。 */
			float InterpKeys(const std::vector<std::pair<float, float>>& keys, const float t)
			{
				if (keys.empty()) {
					return 1.0f;
				}
				if (t <= keys.front().first)  { return keys.front().second; }
				if (t >= keys.back().first)   { return keys.back().second; }
				for (size_t i = 0; i + 1 < keys.size(); ++i) {
					if (t < keys[i + 1].first) {
						float span = keys[i + 1].first - keys[i].first;
						if (span <= 0.0f) { return keys[i].second; }
						float f = (t - keys[i].first) / span;
						return keys[i].second + (keys[i + 1].second - keys[i].second) * f;
					}
				}
				return keys.back().second;
			}


			/** Gradient を 64 サンプル RGBA LUT へ焼き込む (仕様 §4.4)。 */
			void BakeGradient(const JsonValue& j, std::vector<math::Vector4>& lut)
			{
				// カラーキー (RGB)
				std::vector<std::pair<float, float>> rKeys, gKeys, bKeys, aKeys;
				const JsonValue& colorKeys = j["colorKeys"];
				if (colorKeys.IsArray()) {
					for (size_t i = 0; i < colorKeys.Size(); ++i) {
						const JsonValue& k = colorKeys[i];
						float t = k["time"].AsFloat(0.0f);
						math::Vector4 c = ParseVec4(k["color"], math::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
						rKeys.emplace_back(t, c.x);
						gKeys.emplace_back(t, c.y);
						bKeys.emplace_back(t, c.z);
					}
				}
				const JsonValue& alphaKeys = j["alphaKeys"];
				if (alphaKeys.IsArray()) {
					for (size_t i = 0; i < alphaKeys.Size(); ++i) {
						const JsonValue& k = alphaKeys[i];
						aKeys.emplace_back(k["time"].AsFloat(0.0f), k["alpha"].AsFloat(1.0f));
					}
				}

				lut.resize(LUT_SAMPLE_COUNT);
				for (uint32_t i = 0; i < LUT_SAMPLE_COUNT; ++i) {
					float t = static_cast<float>(i) / static_cast<float>(LUT_SAMPLE_COUNT - 1u);
					lut[i]  = math::Vector4(
						rKeys.empty() ? 1.0f : InterpKeys(rKeys, t),
						gKeys.empty() ? 1.0f : InterpKeys(gKeys, t),
						bKeys.empty() ? 1.0f : InterpKeys(bKeys, t),
						aKeys.empty() ? 1.0f : InterpKeys(aKeys, t));
				}
			}


			SimulationSpace ParseSpace(const JsonValue& j, const SimulationSpace def)
			{
				if (!j.IsString()) { return def; }
				return j.AsString() == "World" ? SimulationSpace::World : SimulationSpace::Local;
			}


			void ParseEmitter(const JsonValue& e, EmitterData& out)
			{
				std::vector<float>& pool = out.curveLutPool;

				// 基本 (§7.1)
				if (e["name"].IsString()) { out.name = e["name"].AsString(); }
				out.localPosition      = ParseVec3(e["localPosition"],      out.localPosition);
				out.localRotationEuler = ParseVec3(e["localRotationEuler"], out.localRotationEuler);
				out.duration           = e["duration"].AsFloat(out.duration);
				if (out.duration <= 0.0f) { out.duration = 5.0f; }   // 0 以下は補正 (§7.1)
				out.looping            = e["looping"].AsBool(true);
				out.startDelay         = e["startDelay"].AsFloat(0.0f);
				out.maxParticles       = e["maxParticles"].AsInt(out.maxParticles);
				out.simulationSpace    = ParseSpace(e["simulationSpace"], SimulationSpace::Local);
				out.gravityModifier    = ParseScalar(e["gravityModifier"], pool, 0.0f);

				// initial (§7.2)
				{
					const JsonValue& j = e["initial"];
					out.initial.lifetime = ParseScalar(j["lifetime"], pool, 5.0f);
					out.initial.speed    = ParseScalar(j["speed"],    pool, 5.0f);
					out.initial.size     = ParseScalar(j["size"],     pool, 1.0f);
					out.initial.rotation = ParseScalar(j["rotation"], pool, 0.0f);
					out.initial.color    = ParseColorValue(j["color"], math::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
				}

				// emission (§7.3)
				{
					const JsonValue& j = e["emission"];
					out.emission.enabled      = j["enabled"].AsBool(true);
					out.emission.rateOverTime = ParseScalar(j["rateOverTime"], pool, 10.0f);
					const JsonValue& bursts = j["bursts"];
					if (bursts.IsArray()) {
						for (size_t i = 0; i < bursts.Size(); ++i) {
							const JsonValue& b = bursts[i];
							EmissionBurst burst;
							burst.time        = b["time"].AsFloat(0.0f);
							burst.count       = ParseScalar(b["count"], pool, 0.0f);
							burst.cycles      = b["cycles"].AsInt(1);
							burst.interval    = b["interval"].AsFloat(0.01f);
							burst.probability = b["probability"].AsFloat(1.0f);
							out.emission.bursts.push_back(burst);
						}
					}
				}

				// shape (§7.4)
				{
					const JsonValue& j = e["shape"];
					out.shape.enabled         = j["enabled"].AsBool(true);
					const std::string& type   = j["type"].AsString();
					if      (type == "Sphere") { out.shape.type = ShapeType::Sphere; }
					else if (type == "Box")    { out.shape.type = ShapeType::Box; }
					else if (type == "Circle") { out.shape.type = ShapeType::Circle; }
					else                       { out.shape.type = ShapeType::Cone; }
					out.shape.angle           = j["angle"].AsFloat(25.0f);
					out.shape.radius          = j["radius"].AsFloat(1.0f);
					out.shape.radiusThickness = j["radiusThickness"].AsFloat(1.0f);
					out.shape.boxSize         = ParseVec3(j["boxSize"],       out.shape.boxSize);
					out.shape.position        = ParseVec3(j["position"],      out.shape.position);
					out.shape.rotationEuler   = ParseVec3(j["rotationEuler"], out.shape.rotationEuler);
				}

				// velocityOverLifetime (§7.5)
				{
					const JsonValue& j = e["velocityOverLifetime"];
					out.velocityOverLifetime.enabled = j["enabled"].AsBool(false);
					out.velocityOverLifetime.linearX = ParseScalar(j["linearX"], pool, 0.0f);
					out.velocityOverLifetime.linearY = ParseScalar(j["linearY"], pool, 0.0f);
					out.velocityOverLifetime.linearZ = ParseScalar(j["linearZ"], pool, 0.0f);
					out.velocityOverLifetime.space   = ParseSpace(j["space"], SimulationSpace::Local);
				}

				// colorOverLifetime (§7.6)
				{
					const JsonValue& j = e["colorOverLifetime"];
					out.colorOverLifetime.enabled = j["enabled"].AsBool(false);
					if (out.colorOverLifetime.enabled) {
						BakeGradient(j["gradient"], out.colorOverLifetime.gradientLut);
					}
				}

				// sizeOverLifetime (§7.7)
				{
					const JsonValue& j = e["sizeOverLifetime"];
					out.sizeOverLifetime.enabled = j["enabled"].AsBool(false);
					out.sizeOverLifetime.size    = ParseScalar(j["size"], pool, 1.0f);
				}

				// rotationOverLifetime (§7.8)
				{
					const JsonValue& j = e["rotationOverLifetime"];
					out.rotationOverLifetime.enabled         = j["enabled"].AsBool(false);
					out.rotationOverLifetime.angularVelocity = ParseScalar(j["angularVelocity"], pool, 0.0f);
				}

				// textureSheetAnimation (§7.9)
				{
					const JsonValue& j = e["textureSheetAnimation"];
					out.textureSheetAnimation.enabled       = j["enabled"].AsBool(false);
					out.textureSheetAnimation.tilesX        = j["tilesX"].AsInt(1);
					out.textureSheetAnimation.tilesY        = j["tilesY"].AsInt(1);
					out.textureSheetAnimation.frameOverTime = ParseScalar(j["frameOverTime"], pool, 0.0f);
					out.textureSheetAnimation.startFrame    = ParseScalar(j["startFrame"],    pool, 0.0f);
					out.textureSheetAnimation.cycles        = j["cycles"].AsInt(1);
				}

				// renderer (§7.10)
				{
					const JsonValue& j = e["renderer"];
					const std::string& type = j["type"].AsString();
					if      (type == "StretchedBillboard") { out.renderer.type = RendererType::StretchedBillboard; }
					else if (type == "Mesh")               { out.renderer.type = RendererType::Mesh; }
					else                                   { out.renderer.type = RendererType::Billboard; }
					if (j["texture"].IsString()) { out.renderer.texture = j["texture"].AsString(); }
					if (j["mesh"].IsString())    { out.renderer.mesh    = j["mesh"].AsString(); }
					out.renderer.blendMode   = (j["blendMode"].AsString() == "Additive")
					                         ? BlendMode::Additive : BlendMode::Alpha;
					out.renderer.sortMode    = (j["sortMode"].AsString() == "ByDistance")
					                         ? SortMode::ByDistance : SortMode::None;
					out.renderer.lengthScale = j["lengthScale"].AsFloat(2.0f);
					out.renderer.speedScale  = j["speedScale"].AsFloat(0.0f);
				}
			}
		}


		bool ParticleLoader::Loading()
		{
			ParticleSystemData* resource = static_cast<ParticleSystemData*>(resource_.get());
			if (resource == nullptr) {
				return false;
			}
			ParticleSystemInfo* info = resource->GetWritable();
			if (info == nullptr) {
				return false;
			}

			JsonValue root = aq::util::JsonParser::ParseFile(requestPath_.c_str());
			if (!root.IsObject()) {
				return false;
			}

			// version は 1 のみ受理 (仕様 §2)
			if (root["version"].AsInt(0) != 1) {
				return false;
			}

			info->name = root["name"].IsString() ? root["name"].AsString() : "";

			const JsonValue& warnings = root["warnings"];
			if (warnings.IsArray()) {
				for (size_t i = 0; i < warnings.Size(); ++i) {
					if (warnings[i].IsString()) {
						info->warnings.push_back(warnings[i].AsString());
					}
				}
			}

			const JsonValue& emitters = root["emitters"];
			if (!emitters.IsArray() || emitters.Size() == 0) {
				return false;   // エミッタ 1 件以上必須 (仕様 §6)
			}
			info->emitters.reserve(emitters.Size());
			for (size_t i = 0; i < emitters.Size(); ++i) {
				EmitterData data;
				ParseEmitter(emitters[i], data);
				info->emitters.push_back(std::move(data));
			}

			return true;
		}
	}
}
