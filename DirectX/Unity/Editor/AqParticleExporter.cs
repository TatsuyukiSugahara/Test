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
	/// 選択オブジェクト/プレハブ配下の ParticleSystem を aqEngine の .particle v1 へ書き出す。
	/// 参照テクスチャは出力先の Textures/ へフラットコピーし、renderer.texture を
	/// "Textures/&lt;file&gt;" (=.particle 相対) へ書き換える。バンドルをそのまま
	/// Game/Assets 下に置けばエンジンが .particle 相対でテクスチャを解決する。
	/// 仕様: 設計書/particleフォーマット仕様v1.md（本書が正）。
	/// </summary>
	public static class AqParticleExporter
	{
		// JSON は Infinity を表現できないため、ステップ（Infinity）タンジェントは ±1e30 に置換する（仕様 §4.2）。
		private const float STEP_TANGENT = 1.0e30f;

		private static List<string> _warnings;
		private static Dictionary<string, string> _texMap;    // Unity asset パス -> "Textures/<file>"
		private static HashSet<string> _usedTexNames;         // 出力ファイル名の衝突回避
		private static Dictionary<Mesh, string> _meshMap;     // Mesh -> "Meshes/<file>.obj"
		private static HashSet<string> _usedMeshNames;


		// 選択中の 1 オブジェクトを 1 ファイルへ。テクスチャは同フォルダの Textures/ へ。
		[MenuItem("Tools/aqEngine/Export ParticleSystem")]
		private static void ExportSelected()
		{
			GameObject root = Selection.activeGameObject;
			if (root == null || root.GetComponentsInChildren<ParticleSystem>(true).Length == 0)
			{
				EditorUtility.DisplayDialog("aqEngine", "ParticleSystem を含むオブジェクトを選択してください。", "OK");
				return;
			}

			string path = EditorUtility.SaveFilePanel("Export .particle", "", root.name + ".particle", "particle");
			if (string.IsNullOrEmpty(path)) { return; }
			string outDir = Path.GetDirectoryName(path);

			BeginExport();
			ExportRoot(root, path);
			int copied = CopyTextures(outDir);
			int meshes = WriteMeshes(outDir);
			AssetDatabase.Refresh();
			Debug.Log($"[aqEngine] Exported '{root.name}' + {copied} texture(s) + {meshes} mesh(es) to {outDir}");
		}


		// 選択中の複数オブジェクト/プレハブを、指定フォルダへ一括出力。
		// テクスチャは全効果で共有の Textures/ へまとめてコピー (重複は 1 回)。
		[MenuItem("Tools/aqEngine/Export ParticleSystem (Batch)")]
		private static void ExportBatch()
		{
			var roots = new List<GameObject>();
			foreach (GameObject go in Selection.gameObjects)
			{
				if (go != null && go.GetComponentsInChildren<ParticleSystem>(true).Length > 0)
					roots.Add(go);
			}
			if (roots.Count == 0)
			{
				EditorUtility.DisplayDialog("aqEngine", "選択に ParticleSystem を含むオブジェクトがありません。\n(Project でプレハブを複数選択してください)", "OK");
				return;
			}

			string outDir = EditorUtility.SaveFolderPanel("Export .particle (batch) — 出力フォルダを選択", "", "");
			if (string.IsNullOrEmpty(outDir)) { return; }

			BeginExport();
			int n = 0;
			foreach (GameObject root in roots)
			{
				string file = Path.Combine(outDir, SanitizeFileName(root.name) + ".particle");
				ExportRoot(root, file);
				n++;
			}
			int copied = CopyTextures(outDir);
			int meshes = WriteMeshes(outDir);
			AssetDatabase.Refresh();
			Debug.Log($"[aqEngine] Batch exported {n} effect(s) + {copied} texture(s) + {meshes} mesh(es) to {outDir} (warnings 総数: {_warnings.Count})");
		}


		private static void BeginExport()
		{
			_warnings = new List<string>();
			_texMap = new Dictionary<string, string>();
			_usedTexNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			_meshMap = new Dictionary<Mesh, string>();
			_usedMeshNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		}


		// 1 ルートを 1 .particle ファイルへ書き出す (テクスチャ登録は _texMap へ蓄積)。
		private static void ExportRoot(GameObject root, string outPath)
		{
			ParticleSystem[] systems = root.GetComponentsInChildren<ParticleSystem>(true);

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
						{ "version", "1.1" },
						{ "unity", Application.unityVersion },
						{ "date", DateTime.Now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture) },
					}
				},
				{ "warnings", _warnings.ConvertAll(w => (object)w) },
				{ "emitters", emitters },
			};

			var sb = new StringBuilder();
			Serialize(jroot, sb, 0);
			File.WriteAllText(outPath, sb.ToString(), new UTF8Encoding(false));
		}


		// mainTexture を _texMap へ登録し、.particle に書く相対パス "Textures/<file>" を返す。
		private static string RegisterTexture(Texture tex)
		{
			string src = AssetDatabase.GetAssetPath(tex);
			if (string.IsNullOrEmpty(src)) { return ""; }
			if (_texMap.TryGetValue(src, out string existing)) { return existing; }

			string fileName = Path.GetFileName(src);
			string bare = Path.GetFileNameWithoutExtension(fileName);
			string ext  = Path.GetExtension(fileName);
			string candidate = fileName;
			int i = 1;
			while (_usedTexNames.Contains(candidate)) { candidate = $"{bare}_{i}{ext}"; i++; }
			_usedTexNames.Add(candidate);

			string rel = "Textures/" + candidate;
			_texMap[src] = rel;
			return rel;
		}


		// 登録済みテクスチャを <outDir>/Textures/ へコピー。コピー数を返す。
		private static int CopyTextures(string outDir)
		{
			if (_texMap.Count == 0) { return 0; }
			string texDir = Path.Combine(outDir, "Textures");
			Directory.CreateDirectory(texDir);

			int copied = 0;
			foreach (KeyValuePair<string, string> kv in _texMap)
			{
				try
				{
					string abs = ToAbsolutePath(kv.Key);           // Unity asset -> 実ファイル
					string dst = Path.Combine(outDir, kv.Value);    // <outDir>/Textures/<file>
					File.Copy(abs, dst, true);
					copied++;
				}
				catch (Exception e)
				{
					_warnings.Add($"texture copy 失敗: {kv.Key} ({e.Message})");
					Debug.LogWarning($"[aqEngine] texture copy 失敗: {kv.Key} — {e.Message}");
				}
			}
			return copied;
		}


		// Unity アセットパス ("Assets/..." / "Packages/...") を OS 絶対パスへ。
		private static string ToAbsolutePath(string assetPath)
		{
			if (assetPath.StartsWith("Assets/"))
				return Path.Combine(Application.dataPath, assetPath.Substring("Assets/".Length));
			// Packages/ など: プロジェクトルート (= dataPath の親) 起点
			string projRoot = Directory.GetParent(Application.dataPath).FullName;
			return Path.Combine(projRoot, assetPath);
		}


		private static string SanitizeFileName(string name)
		{
			foreach (char c in Path.GetInvalidFileNameChars())
				name = name.Replace(c, '_');
			return name;
		}


		// Mesh を _meshMap へ登録し、.particle に書く相対パス "Meshes/<file>.obj" を返す。
		private static string RegisterMesh(Mesh m)
		{
			if (_meshMap.TryGetValue(m, out string existing)) { return existing; }

			string bare = SanitizeFileName(string.IsNullOrEmpty(m.name) ? "mesh" : m.name);
			string candidate = bare + ".obj";
			int i = 1;
			while (_usedMeshNames.Contains(candidate)) { candidate = $"{bare}_{i}.obj"; i++; }
			_usedMeshNames.Add(candidate);

			string rel = "Meshes/" + candidate;
			_meshMap[m] = rel;
			return rel;
		}


		// 登録済みメッシュを <outDir>/Meshes/ へ OBJ 出力。出力数を返す。
		private static int WriteMeshes(string outDir)
		{
			if (_meshMap.Count == 0) { return 0; }
			string meshDir = Path.Combine(outDir, "Meshes");
			Directory.CreateDirectory(meshDir);

			int written = 0;
			foreach (KeyValuePair<Mesh, string> kv in _meshMap)
			{
				try
				{
					File.WriteAllText(Path.Combine(outDir, kv.Value), MeshToObj(kv.Key), new UTF8Encoding(false));
					written++;
				}
				catch (Exception e)
				{
					_warnings.Add($"mesh 書き出し失敗: {kv.Key.name} ({e.Message})");
					Debug.LogWarning($"[aqEngine] mesh 書き出し失敗: {kv.Key.name} — {e.Message}");
				}
			}
			return written;
		}


		// Unity Mesh を OBJ 文字列へ。座標系は Unity/DX とも左手 Y-up なのでそのまま。
		// エンジン側 (.obj ローダー) で V 反転・winding を吸収する。
		private static string MeshToObj(Mesh m)
		{
			var sb = new StringBuilder();
			sb.Append("# exported by AqParticleExporter\n");
			sb.Append("o ").Append(m.name).Append('\n');

			Vector3[] verts = m.vertices;
			Vector3[] norms = m.normals;
			Vector2[] uvs   = m.uv;
			bool hasN = norms != null && norms.Length == verts.Length;
			bool hasT = uvs != null && uvs.Length == verts.Length;

			var ci = CultureInfo.InvariantCulture;
			foreach (Vector3 v in verts)
				sb.Append("v ").Append(v.x.ToString("R", ci)).Append(' ').Append(v.y.ToString("R", ci)).Append(' ').Append(v.z.ToString("R", ci)).Append('\n');
			if (hasT)
				foreach (Vector2 t in uvs)
					sb.Append("vt ").Append(t.x.ToString("R", ci)).Append(' ').Append(t.y.ToString("R", ci)).Append('\n');
			if (hasN)
				foreach (Vector3 n in norms)
					sb.Append("vn ").Append(n.x.ToString("R", ci)).Append(' ').Append(n.y.ToString("R", ci)).Append(' ').Append(n.z.ToString("R", ci)).Append('\n');

			// 全サブメッシュの三角形をまとめて出力 (OBJ は 1-indexed)。
			for (int s = 0; s < m.subMeshCount; ++s)
			{
				int[] tris = m.GetTriangles(s);
				for (int k = 0; k + 2 < tris.Length; k += 3)
				{
					int a = tris[k] + 1, b = tris[k + 1] + 1, c = tris[k + 2] + 1;
					sb.Append('f').Append(' ').Append(FaceVert(a, hasT, hasN))
					              .Append(' ').Append(FaceVert(b, hasT, hasN))
					              .Append(' ').Append(FaceVert(c, hasT, hasN)).Append('\n');
				}
			}
			return sb.ToString();
		}

		private static string FaceVert(int idx, bool hasT, bool hasN)
		{
			// v / v/vt / v//vn / v/vt/vn
			if (hasT && hasN) { return $"{idx}/{idx}/{idx}"; }
			if (hasT)         { return $"{idx}/{idx}"; }
			if (hasN)         { return $"{idx}//{idx}"; }
			return idx.ToString();
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

			var initial = new Dictionary<string, object>
			{
				{ "lifetime", Curve(main.startLifetime) },
				{ "size",     Curve(main.startSize3D ? main.startSizeX : main.startSize) },
				{ "speed",    Curve(main.startSpeed) },
				{ "rotation", Curve(main.startRotation3D ? main.startRotationZ : main.startRotation, Mathf.Rad2Deg) },   // Unity はラジアン
				{ "color",    ColorValue(main.startColor) },
			};
			// 3D Start Size: 軸別サイズ (ビーム板/円筒メッシュの非一様スケールに必須)
			if (main.startSize3D)
			{
				initial["size3D"] = new Dictionary<string, object>
				{
					{ "x", Curve(main.startSizeX) },
					{ "y", Curve(main.startSizeY) },
					{ "z", Curve(main.startSizeZ) },
				};
			}
			// 3D Start Rotation: 軸別回転 (度)。ビーム板/円筒を立てる・寝かせる向き付けに必須
			if (main.startRotation3D)
			{
				initial["rotation3D"] = new Dictionary<string, object>
				{
					{ "x", Curve(main.startRotationX, Mathf.Rad2Deg) },
					{ "y", Curve(main.startRotationY, Mathf.Rad2Deg) },
					{ "z", Curve(main.startRotationZ, Mathf.Rad2Deg) },
				};
			}

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
				{ "initial", initial },
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
				var sizeOL = new Dictionary<string, object>
				{
					{ "enabled", true },
					{ "size", Curve(siz.separateAxes ? siz.x : siz.size) },
				};
				// Separate Axes: 軸別の倍率カーブ (ビーム板が寿命中に細く/短くなる表現に必須)
				if (siz.separateAxes)
				{
					sizeOL["size3D"] = new Dictionary<string, object>
					{
						{ "x", Curve(siz.x) },
						{ "y", Curve(siz.y) },
						{ "z", Curve(siz.z) },
					};
				}
				e["sizeOverLifetime"] = sizeOL;
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
			string mesh = "";
			string blend = "Alpha";
			string sort = "None";
			float lengthScale = 2.0f;
			float speedScale = 0.0f;
			Vector2 uvScale = Vector2.one;
			Vector2 uvOffset = Vector2.zero;

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

				// Mesh レンダラー: メッシュ本体を OBJ 出力し相対パスを書く (P6)。
				if (r.renderMode == ParticleSystemRenderMode.Mesh && r.mesh != null)
					mesh = RegisterMesh(r.mesh);

				Material m = r.sharedMaterial;
				if (m != null)
				{
					if (m.mainTexture != null) { texture = RegisterTexture(m.mainTexture); }
					blend = DetectBlend(m);

					// マテリアルの Tiling/Offset。細い帯や1装飾だけを切り出すマテリアルを再現する。
					// Unity の UV は左下原点、aq (DirectX) は左上原点なので V を変換して出力する。
					Vector2 ts = m.mainTextureScale;
					Vector2 to = m.mainTextureOffset;
					uvScale  = ts;
					uvOffset = new Vector2(to.x, 1.0f - to.y - ts.y);
				}
			}
			return new Dictionary<string, object>
			{
				{ "type", type },
				{ "texture", texture },
				{ "mesh", mesh },
				{ "blendMode", blend },
				{ "sortMode", sort },
				{ "lengthScale", lengthScale },
				{ "speedScale", speedScale },
				{ "uvScale",  new List<object> { (float)uvScale.x, (float)uvScale.y } },
				{ "uvOffset", new List<object> { (float)uvOffset.x, (float)uvOffset.y } },
			};
		}


		// マテリアルのブレンドが加算かを推定する。v1 は Alpha / Additive の 2 値。
		// Legacy/Mobile の固定機能ブレンドは _DstBlend を持たないためシェーダ名でも判定する。
		private static string DetectBlend(Material m)
		{
			if (m == null) { return "Alpha"; }

			// シェーダ名ベース（"Legacy Shaders/Particles/Additive" 等）
			string shaderName = m.shader != null ? m.shader.name.ToLowerInvariant() : "";
			if (shaderName.Contains("additive")) { return "Additive"; }

			// Built-in/URP パーティクル: _DstBlend == One
			if (m.HasProperty("_DstBlend") &&
			    Mathf.RoundToInt(m.GetFloat("_DstBlend")) == (int)UnityEngine.Rendering.BlendMode.One)
			{
				return "Additive";
			}
			// URP パーティクル: _Blend 2 = Additive（0=Alpha,1=Premultiply,2=Additive,3=Multiply）
			if (m.HasProperty("_Blend") && Mathf.RoundToInt(m.GetFloat("_Blend")) == 2)
			{
				return "Additive";
			}
			return "Alpha";
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
