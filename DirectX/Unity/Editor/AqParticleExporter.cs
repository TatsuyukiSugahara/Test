#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace AqEngine.ParticleExport
{
	/// <summary>
	/// 選択オブジェクト配下の ParticleSystem を aqEngine の .particle v1 へ書き出す。
	/// 仕様: aqEngine/Particle/particleフォーマット仕様v1.md（本書が正）。
	/// </summary>
	public static class AqParticleExporter
	{
		// JSON は Infinity を表現できないため、ステップ（Infinity）タンジェントは ±1e30 に置換する（仕様 §4.2）。
		private const float STEP_TANGENT = 1.0e30f;

		private static List<string> _warnings;

		[MenuItem("Tools/aqEngine/Export ParticleSystem")]
		private static void Export()
		{
			GameObject root = Selection.activeGameObject;
			if (root == null)
			{
				EditorUtility.DisplayDialog("aqEngine", "ParticleSystem を含むオブジェクトを選択してください。", "OK");
				return;
			}

			ParticleSystem[] systems = root.GetComponentsInChildren<ParticleSystem>(true);
			if (systems == null || systems.Length == 0)
			{
				EditorUtility.DisplayDialog("aqEngine", "選択階層に ParticleSystem がありません。", "OK");
				return;
			}

			string path = EditorUtility.SaveFilePanel("Export .particle", "", root.name + ".particle", "particle");
			if (string.IsNullOrEmpty(path))
			{
				return;
			}

			_warnings = new List<string>();

			var emitters = new List<object>();
			foreach (ParticleSystem ps in systems)
			{
				emitters.Add(ExportEmitter(ps, root.transform));
			}

			var jroot = new Dictionary<string, object>
			{
				{ "version", 1 },
				{ "name", root.name },
				{ "exporter", new Dictionary<string, object>
					{
						{ "tool", "AqParticleExporter" },
						{ "version", "1.0" },
						{ "unity", Application.unityVersion },
						{ "date", DateTime.Now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture) },
					}
				},
				{ "warnings", _warnings.ConvertAll(w => (object)w) },
				{ "emitters", emitters },
			};

			var sb = new StringBuilder();
			Serialize(jroot, sb, 0);
			File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
			AssetDatabase.Refresh();

			Debug.Log($"[aqEngine] Exported {systems.Length} emitter(s) to {path} (warnings: {_warnings.Count})");
		}


		// ----------------------------------------------------------------------
		// エミッタ単位のエクスポート
		// ----------------------------------------------------------------------
		private static Dictionary<string, object> ExportEmitter(ParticleSystem ps, Transform root)
		{
			ParticleSystem.MainModule main = ps.main;

			// ルートからの相対トランスフォーム
			Vector3 localPos = root.InverseTransformPoint(ps.transform.position);
			Vector3 localEuler = (Quaternion.Inverse(root.rotation) * ps.transform.rotation).eulerAngles;

			WarnUnsupported(ps);

			var e = new Dictionary<string, object>
			{
				{ "name", ps.gameObject.name },
				{ "localPosition", Vec3(localPos) },
				{ "localRotationEuler", Vec3(localEuler) },
				{ "duration", main.duration },
				{ "looping", main.loop },
				{ "startDelay", main.startDelay.constant },
				{ "maxParticles", main.maxParticles },
				{ "simulationSpace", main.simulationSpace == ParticleSystemSimulationSpace.World ? "World" : "Local" },
				{ "gravityModifier", Curve(main.gravityModifier) },
				{ "initial", new Dictionary<string, object>
					{
						{ "lifetime", Curve(main.startLifetime) },
						{ "speed",    Curve(main.startSpeed) },
						{ "size",     Curve(main.startSize) },
						{ "rotation", Curve(main.startRotation, Mathf.Rad2Deg) },   // Unity はラジアン
						{ "color",    ColorValue(main.startColor) },
					}
				},
				{ "emission", ExportEmission(ps) },
				{ "shape", ExportShape(ps) },
				{ "renderer", ExportRenderer(ps) },
			};

			ParticleSystem.VelocityOverLifetimeModule vel = ps.velocityOverLifetime;
			if (vel.enabled)
			{
				e["velocityOverLifetime"] = new Dictionary<string, object>
				{
					{ "enabled", true },
					{ "linearX", Curve(vel.x) },
					{ "linearY", Curve(vel.y) },
					{ "linearZ", Curve(vel.z) },
					{ "space", vel.space == ParticleSystemSimulationSpace.World ? "World" : "Local" },
				};
			}

			ParticleSystem.ColorOverLifetimeModule col = ps.colorOverLifetime;
			if (col.enabled)
			{
				e["colorOverLifetime"] = new Dictionary<string, object>
				{
					{ "enabled", true },
					{ "gradient", GradientValue(col.color) },
				};
			}

			ParticleSystem.SizeOverLifetimeModule siz = ps.sizeOverLifetime;
			if (siz.enabled)
			{
				if (siz.separateAxes) { _warnings.Add($"{ps.name}: sizeOverLifetime Separate Axes は非対応（size 軸のみ使用）"); }
				e["sizeOverLifetime"] = new Dictionary<string, object>
				{
					{ "enabled", true },
					{ "size", Curve(siz.size) },
				};
			}

			ParticleSystem.RotationOverLifetimeModule rot = ps.rotationOverLifetime;
			if (rot.enabled)
			{
				e["rotationOverLifetime"] = new Dictionary<string, object>
				{
					{ "enabled", true },
					{ "angularVelocity", Curve(rot.z, Mathf.Rad2Deg) },   // ラジアン/秒 → 度/秒
				};
			}

			ParticleSystem.TextureSheetAnimationModule tex = ps.textureSheetAnimation;
			if (tex.enabled)
			{
				e["textureSheetAnimation"] = new Dictionary<string, object>
				{
					{ "enabled", true },
					{ "tilesX", tex.numTilesX },
					{ "tilesY", tex.numTilesY },
					{ "frameOverTime", Curve(tex.frameOverTime) },
					{ "startFrame", Curve(tex.startFrame) },
					{ "cycles", tex.cycleCount },
				};
			}

			return e;
		}


		private static Dictionary<string, object> ExportEmission(ParticleSystem ps)
		{
			ParticleSystem.EmissionModule em = ps.emission;
			var bursts = new List<object>();
			var buf = new ParticleSystem.Burst[em.burstCount];
			em.GetBursts(buf);
			foreach (ParticleSystem.Burst b in buf)
			{
				bursts.Add(new Dictionary<string, object>
				{
					{ "time", b.time },
					{ "count", Curve(b.count) },
					{ "cycles", b.cycleCount },
					{ "interval", b.repeatInterval },
					{ "probability", b.probability },
				});
			}
			return new Dictionary<string, object>
			{
				{ "enabled", em.enabled },
				{ "rateOverTime", Curve(em.rateOverTime) },
				{ "bursts", bursts },
			};
		}


		private static Dictionary<string, object> ExportShape(ParticleSystem ps)
		{
			ParticleSystem.ShapeModule sh = ps.shape;
			string type;
			switch (sh.shapeType)
			{
				case ParticleSystemShapeType.Sphere:            type = "Sphere"; break;
				case ParticleSystemShapeType.Hemisphere:        type = "Sphere"; _warnings.Add($"{ps.name}: Hemisphere は Sphere に近似"); break;
				case ParticleSystemShapeType.Box:               type = "Box"; break;
				case ParticleSystemShapeType.Circle:            type = "Circle"; break;
				case ParticleSystemShapeType.Cone:              type = "Cone"; break;
				default:                                        type = "Sphere"; _warnings.Add($"{ps.name}: shape {sh.shapeType} は Sphere に近似"); break;
			}
			return new Dictionary<string, object>
			{
				{ "enabled", sh.enabled },
				{ "type", type },
				{ "angle", sh.angle },
				{ "radius", sh.radius },
				{ "radiusThickness", sh.radiusThickness },
				{ "boxSize", Vec3(sh.scale) },
				{ "position", Vec3(sh.position) },
				{ "rotationEuler", Vec3(sh.rotation) },
			};
		}


		private static Dictionary<string, object> ExportRenderer(ParticleSystem ps)
		{
			var r = ps.GetComponent<ParticleSystemRenderer>();
			string type = "Billboard";
			string texture = "";
			string blend = "Alpha";
			string sort = "None";
			float lengthScale = 2.0f;
			float speedScale = 0.0f;

			if (r != null)
			{
				switch (r.renderMode)
				{
					case ParticleSystemRenderMode.Stretch: type = "StretchedBillboard"; break;
					case ParticleSystemRenderMode.Mesh:    type = "Mesh"; break;
					default:                               type = "Billboard"; break;
				}
				lengthScale = r.lengthScale;
				speedScale = r.velocityScale;
				sort = r.sortMode == ParticleSystemSortMode.None ? "None" : "ByDistance";

				Material m = r.sharedMaterial;
				if (m != null)
				{
					if (m.mainTexture != null) { texture = AssetDatabase.GetAssetPath(m.mainTexture); }
					if (m.HasProperty("_DstBlend") && Mathf.RoundToInt(m.GetFloat("_DstBlend")) == (int)UnityEngine.Rendering.BlendMode.One)
					{
						blend = "Additive";
					}
				}
			}
			return new Dictionary<string, object>
			{
				{ "type", type },
				{ "texture", texture },
				{ "blendMode", blend },
				{ "sortMode", sort },
				{ "lengthScale", lengthScale },
				{ "speedScale", speedScale },
			};
		}


		// ----------------------------------------------------------------------
		// 共通型のエクスポート（仕様 §4）
		// ----------------------------------------------------------------------
		private static Dictionary<string, object> Curve(ParticleSystem.MinMaxCurve c, float scale = 1.0f)
		{
			switch (c.mode)
			{
				case ParticleSystemCurveMode.TwoConstants:
					return new Dictionary<string, object>
					{
						{ "mode", "TwoConstants" },
						{ "min", c.constantMin * scale },
						{ "max", c.constantMax * scale },
					};
				case ParticleSystemCurveMode.Curve:
					return new Dictionary<string, object>
					{
						{ "mode", "Curve" },
						{ "multiplier", c.curveMultiplier * scale },
						{ "curve", AnimCurve(c.curve) },
					};
				case ParticleSystemCurveMode.TwoCurves:
					return new Dictionary<string, object>
					{
						{ "mode", "TwoCurves" },
						{ "multiplier", c.curveMultiplier * scale },
						{ "curveMin", AnimCurve(c.curveMin) },
						{ "curveMax", AnimCurve(c.curveMax) },
					};
				default:
					return new Dictionary<string, object>
					{
						{ "mode", "Constant" },
						{ "constant", c.constant * scale },
					};
			}
		}


		private static Dictionary<string, object> AnimCurve(AnimationCurve curve)
		{
			var keys = new List<object>();
			if (curve != null)
			{
				foreach (Keyframe k in curve.keys)
				{
					if (k.weightedMode != WeightedMode.None)
					{
						_warnings.Add("weightedMode カーブは非対応（通常タンジェントとして出力）");
					}
					keys.Add(new Dictionary<string, object>
					{
						{ "time", k.time },
						{ "value", k.value },
						{ "inTangent", SanitizeTangent(k.inTangent) },
						{ "outTangent", SanitizeTangent(k.outTangent) },
					});
				}
			}
			return new Dictionary<string, object> { { "keys", keys } };
		}


		private static float SanitizeTangent(float t)
		{
			if (float.IsPositiveInfinity(t)) { return STEP_TANGENT; }
			if (float.IsNegativeInfinity(t)) { return -STEP_TANGENT; }
			return t;
		}


		// startColor 用 ColorValue（仕様 §4.3）
		private static Dictionary<string, object> ColorValue(ParticleSystem.MinMaxGradient g)
		{
			switch (g.mode)
			{
				case ParticleSystemGradientMode.Color:
					return new Dictionary<string, object> { { "mode", "Constant" }, { "color", Col(g.color) } };
				case ParticleSystemGradientMode.TwoColors:
					return new Dictionary<string, object> { { "mode", "TwoConstants" }, { "min", Col(g.colorMin) }, { "max", Col(g.colorMax) } };
				default:
					_warnings.Add($"startColor の {g.mode} は定数に近似");
					return new Dictionary<string, object> { { "mode", "Constant" }, { "color", Col(g.color) } };
			}
		}


		// colorOverLifetime 用 Gradient（仕様 §4.4）
		private static Dictionary<string, object> GradientValue(ParticleSystem.MinMaxGradient g)
		{
			Gradient grad = g.gradient;
			if (g.mode == ParticleSystemGradientMode.TwoGradients)
			{
				_warnings.Add("colorOverLifetime TwoGradients は max 側のみ使用");
				grad = g.gradientMax;
			}
			else if (g.mode == ParticleSystemGradientMode.Color)
			{
				// 単色 → 2 キーのフラットグラデーション扱い
				var single = new Gradient();
				single.SetKeys(
					new[] { new GradientColorKey(g.color, 0f), new GradientColorKey(g.color, 1f) },
					new[] { new GradientAlphaKey(g.color.a, 0f), new GradientAlphaKey(g.color.a, 1f) });
				grad = single;
			}

			var colorKeys = new List<object>();
			var alphaKeys = new List<object>();
			if (grad != null)
			{
				foreach (GradientColorKey ck in grad.colorKeys)
				{
					colorKeys.Add(new Dictionary<string, object>
					{
						{ "time", ck.time },
						{ "color", new List<object> { (float)ck.color.r, (float)ck.color.g, (float)ck.color.b } },
					});
				}
				foreach (GradientAlphaKey ak in grad.alphaKeys)
				{
					alphaKeys.Add(new Dictionary<string, object> { { "time", ak.time }, { "alpha", ak.alpha } });
				}
			}
			return new Dictionary<string, object> { { "colorKeys", colorKeys }, { "alphaKeys", alphaKeys } };
		}


		private static List<object> Vec3(Vector3 v) => new List<object> { (float)v.x, (float)v.y, (float)v.z };
		private static List<object> Col(Color c) => new List<object> { (float)c.r, (float)c.g, (float)c.b, (float)c.a };


		private static void WarnUnsupported(ParticleSystem ps)
		{
			if (ps.trails.enabled)                    { _warnings.Add($"{ps.name}: Trails は非対応"); }
			if (ps.noise.enabled)                     { _warnings.Add($"{ps.name}: Noise は非対応"); }
			if (ps.subEmitters.enabled)               { _warnings.Add($"{ps.name}: Sub Emitters は非対応"); }
			if (ps.collision.enabled)                 { _warnings.Add($"{ps.name}: Collision は非対応"); }
			if (ps.trigger.enabled)                   { _warnings.Add($"{ps.name}: Triggers は非対応"); }
			if (ps.forceOverLifetime.enabled)         { _warnings.Add($"{ps.name}: Force over Lifetime は非対応"); }
			if (ps.inheritVelocity.enabled)           { _warnings.Add($"{ps.name}: Inherit Velocity は非対応"); }
			if (ps.limitVelocityOverLifetime.enabled) { _warnings.Add($"{ps.name}: Limit Velocity は非対応"); }
			if (ps.externalForces.enabled)            { _warnings.Add($"{ps.name}: External Forces は非対応"); }
			if (ps.lights.enabled)                    { _warnings.Add($"{ps.name}: Lights は非対応"); }
			if (ps.main.startSize3D)                  { _warnings.Add($"{ps.name}: 3D Start Size は非対応（size 軸のみ使用）"); }
		}


		// ----------------------------------------------------------------------
		// 最小 JSON シリアライザ（InvariantCulture・Infinity 非出力）
		// ----------------------------------------------------------------------
		private static void Serialize(object v, StringBuilder sb, int indent)
		{
			switch (v)
			{
				case null:
					sb.Append("null");
					break;
				case string s:
					sb.Append('"').Append(Escape(s)).Append('"');
					break;
				case bool b:
					sb.Append(b ? "true" : "false");
					break;
				case int i:
					sb.Append(i.ToString(CultureInfo.InvariantCulture));
					break;
				case float f:
					sb.Append(FloatStr(f));
					break;
				case double d:
					sb.Append(FloatStr((float)d));
					break;
				case Dictionary<string, object> obj:
					SerializeObject(obj, sb, indent);
					break;
				case List<object> arr:
					SerializeArray(arr, sb, indent);
					break;
				default:
					sb.Append('"').Append(Escape(v.ToString())).Append('"');
					break;
			}
		}

		private static void SerializeObject(Dictionary<string, object> obj, StringBuilder sb, int indent)
		{
			sb.Append("{\n");
			int n = 0;
			string pad = new string(' ', (indent + 1) * 2);
			foreach (var kv in obj)
			{
				sb.Append(pad).Append('"').Append(Escape(kv.Key)).Append("\": ");
				Serialize(kv.Value, sb, indent + 1);
				if (++n < obj.Count) { sb.Append(','); }
				sb.Append('\n');
			}
			sb.Append(new string(' ', indent * 2)).Append('}');
		}

		private static void SerializeArray(List<object> arr, StringBuilder sb, int indent)
		{
			if (arr.Count == 0) { sb.Append("[]"); return; }
			sb.Append("[\n");
			string pad = new string(' ', (indent + 1) * 2);
			for (int i = 0; i < arr.Count; ++i)
			{
				sb.Append(pad);
				Serialize(arr[i], sb, indent + 1);
				if (i + 1 < arr.Count) { sb.Append(','); }
				sb.Append('\n');
			}
			sb.Append(new string(' ', indent * 2)).Append(']');
		}

		private static string FloatStr(float f)
		{
			if (float.IsPositiveInfinity(f)) { return STEP_TANGENT.ToString("R", CultureInfo.InvariantCulture); }
			if (float.IsNegativeInfinity(f)) { return (-STEP_TANGENT).ToString("R", CultureInfo.InvariantCulture); }
			if (float.IsNaN(f)) { return "0"; }
			return f.ToString("0.######", CultureInfo.InvariantCulture);
		}

		private static string Escape(string s)
		{
			return s.Replace("\\", "\\\\").Replace("\"", "\\\"");
		}
	}
}
#endif
