#include "stdafx.h"
#include "StageData.h"
#include "Util/SimpleJson.h"


namespace app
{
	namespace stage
	{
		namespace
		{
			// 1 セグメントあたりの弧長サンプル数。カーブ半径に対して十分細かい値。
			static constexpr int SEGMENT_SAMPLE_COUNT = 32;


			// Catmull-Rom 補間 (t: 0..1)。
			aq::math::Vector3 CatmullRom(
				const aq::math::Vector3& p0, const aq::math::Vector3& p1,
				const aq::math::Vector3& p2, const aq::math::Vector3& p3, const float t)
			{
				const float t2 = t * t;
				const float t3 = t2 * t;
				return (p1 * 2.0f
					+ (p2 - p0) * t
					+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
					+ ((p1 - p2) * 3.0f + p3 - p0) * t3) * 0.5f;
			}


			// JSON 配列 [x, y, z] を Vector3 として読む。
			aq::math::Vector3 ReadVector3(const aq::util::JsonValue& v)
			{
				return aq::math::Vector3(v[0u].AsFloat(), v[1u].AsFloat(), v[2u].AsFloat());
			}
		}


		/**
		 * コーススプライン
		 */
		void CourseSpline::Build(const std::vector<aq::math::Vector3>& points, const std::vector<aq::math::Vector3>& ups)
		{
			samples_.clear();
			totalLength_ = 0.0f;
			if (points.size() < 2 || points.size() != ups.size()) { return; }

			const int pointCount   = static_cast<int>(points.size());
			const int segmentCount = pointCount - 1;
			samples_.reserve(static_cast<size_t>(segmentCount) * SEGMENT_SAMPLE_COUNT + 1);

			// 端点は複製して Catmull-Rom の範囲外参照を防ぐ。
			auto pointAt = [&](const int i) -> const aq::math::Vector3&
				{
					const int clamped = i < 0 ? 0 : (i >= pointCount ? pointCount - 1 : i);
					return points[clamped];
				};

			aq::math::Vector3 prevPos = points[0];
			for (int seg = 0; seg < segmentCount; ++seg) {
				const int sampleEnd = (seg == segmentCount - 1) ? SEGMENT_SAMPLE_COUNT : SEGMENT_SAMPLE_COUNT - 1;
				for (int s = 0; s <= sampleEnd; ++s) {
					const float t = static_cast<float>(s) / static_cast<float>(SEGMENT_SAMPLE_COUNT);

					Sample sample;
					sample.position = CatmullRom(pointAt(seg - 1), pointAt(seg), pointAt(seg + 1), pointAt(seg + 2), t);

					// 接線は微小前進の差分で近似 (端点は方向を維持)。
					const aq::math::Vector3 aheadPos =
						CatmullRom(pointAt(seg - 1), pointAt(seg), pointAt(seg + 1), pointAt(seg + 2), t + 0.01f);
					sample.tangent = aheadPos - sample.position;
					if (!sample.tangent.TryNormalize()) {
						sample.tangent = samples_.empty() ? aq::math::Vector3(0.0f, 0.0f, 1.0f) : samples_.back().tangent;
					}

					// up は制御点間の線形補間 → 接線と直交化。
					aq::math::Vector3 up = aq::math::Lerp(ups[seg], ups[seg + 1], t);
					aq::math::Vector3 right;
					right.Cross(up, sample.tangent);
					if (!right.TryNormalize()) {
						right.Set(1.0f, 0.0f, 0.0f);
					}
					up.Cross(sample.tangent, right);
					up.Normalize();
					sample.up = up;

					// 弧長を積算。
					if (!samples_.empty()) {
						totalLength_ += (sample.position - prevPos).Length();
					}
					sample.distance = totalLength_;
					prevPos = sample.position;
					samples_.push_back(sample);
				}
			}
		}


		aq::math::Quaternion CourseSpline::Frame::ToRotation() const
		{
			const DirectX::XMMATRIX m(
				right.x,   right.y,   right.z,   0.0f,
				up.x,      up.y,      up.z,      0.0f,
				tangent.x, tangent.y, tangent.z, 0.0f,
				0.0f,      0.0f,      0.0f,      1.0f);
			aq::math::Quaternion result;
			DirectX::XMStoreFloat4(&result.vector, DirectX::XMQuaternionRotationMatrix(m));
			return result;
		}


		CourseSpline::Frame CourseSpline::Evaluate(const float distance) const
		{
			Frame frame;
			if (!IsValid()) { return frame; }

			// 範囲外はクランプ。
			float d = distance;
			if (d < 0.0f)         { d = 0.0f; }
			if (d > totalLength_) { d = totalLength_; }

			// 弧長テーブルを二分探索して区間を特定する。
			auto it = std::lower_bound(samples_.begin(), samples_.end(), d,
				[](const Sample& sample, const float value)
				{
					return sample.distance < value;
				});
			if (it == samples_.begin()) { ++it; }
			const Sample& s1 = *it;
			const Sample& s0 = *(it - 1);

			const float span = s1.distance - s0.distance;
			const float f    = span > 0.0001f ? (d - s0.distance) / span : 0.0f;

			frame.position = aq::math::Lerp(s0.position, s1.position, f);
			frame.tangent  = aq::math::Lerp(s0.tangent,  s1.tangent,  f);
			if (!frame.tangent.TryNormalize()) { frame.tangent = s0.tangent; }
			frame.up = aq::math::Lerp(s0.up, s1.up, f);
			frame.right.Cross(frame.up, frame.tangent);
			if (!frame.right.TryNormalize()) { frame.right.Set(1.0f, 0.0f, 0.0f); }
			frame.up.Cross(frame.tangent, frame.right);
			frame.up.Normalize();
			return frame;
		}


		/************************************/




		/**
		 * ステージ定義
		 */
		std::string StageData::CalcRank(const uint32_t coinCount, const float timeSec) const
		{
			const float coinRate = coins.empty()
				? 1.0f
				: static_cast<float>(coinCount) / static_cast<float>(coins.size());
			const float timeRate = timeSec > 0.0001f
				? (parTimeSec / timeSec < 1.0f ? parTimeSec / timeSec : 1.0f)
				: 1.0f;
			const float score = 0.6f * coinRate + 0.4f * timeRate;

			for (const auto& threshold : ranks) {
				if (score >= threshold.score) { return threshold.rank; }
			}
			return ranks.empty() ? std::string("C") : ranks.back().rank;
		}


		std::shared_ptr<StageData> StageData::LoadFromFile(const char* path)
		{
			const aq::util::JsonValue root = aq::util::JsonParser::ParseFile(path);
			if (!root.IsObject()) { return nullptr; }

			auto data = std::make_shared<StageData>();
			data->levelPath = root["level"].AsString();

			// コース
			{
				const auto& course = root["course"];
				data->width = course["width"].AsFloat(12.0f);

				std::vector<aq::math::Vector3> points;
				std::vector<aq::math::Vector3> ups;
				for (const auto& p : course["points"].GetArray()) {
					points.push_back(ReadVector3(p["position"]));
					ups.push_back(p.Contains("up") ? ReadVector3(p["up"]) : aq::math::Vector3(0.0f, 1.0f, 0.0f));
				}
				data->spline.Build(points, ups);
			}
			if (!data->spline.IsValid()) { return nullptr; }

			// スポーン / 判定
			data->spawnDistance = root["spawn"]["distance"].AsFloat(0.0f);
			for (const auto& lane : root["spawn"]["lanes"].GetArray()) {
				data->spawnLanes.push_back(lane.AsFloat());
			}
			data->goalDistance = root["goal"]["distance"].AsFloat(data->spline.GetTotalLength());
			data->fallHeight   = root["fall"]["heightThreshold"].AsFloat(-30.0f);

			// ゴールが弧長より先にあると到達不能になるため、コース末端の少し手前へクランプする。
			const float maxGoal = data->spline.GetTotalLength() - 5.0f;
			if (data->goalDistance > maxGoal) { data->goalDistance = maxGoal; }

			// 地形 (省略可。ブロックごと無ければ既定値のまま = 平坦 grass)
			if (root.Contains("terrain")) {
				const auto& terrain = root["terrain"];
				data->terrainHeightmapPath = terrain["heightmap"].AsString();
				data->terrainSplatmapPath  = terrain["splatmap"].AsString();
				data->terrainHeightScale   = terrain["heightScale"].AsFloat(0.0f);
				data->terrainHeightOffset  = terrain["heightOffset"].AsFloat(0.0f);
				data->terrainResolution    = static_cast<uint32_t>(terrain["resolution"].AsInt(128));
			}

			// コイン (判定は P2)
			for (const auto& coin : root["coins"].GetArray()) {
				CoinPlacement placement;
				placement.distance = coin["distance"].AsFloat();
				placement.lateral  = coin["lateral"].AsFloat();
				placement.height   = coin["height"].AsFloat(1.0f);
				data->coins.push_back(placement);
			}

			// ランク
			data->parTimeSec = root["rank"]["parTimeSec"].AsFloat(180.0f);
			for (const auto& threshold : root["rank"]["thresholds"].GetArray()) {
				RankThreshold rank;
				rank.rank  = threshold["rank"].AsString();
				rank.score = threshold["score"].AsFloat();
				data->ranks.push_back(rank);
			}
			return data;
		}


		/************************************/




		/**
		 * ステージ一覧
		 */
		std::vector<StageListEntry> StageRegistry::LoadList(const char* path)
		{
			std::vector<StageListEntry> entries;
			const aq::util::JsonValue root = aq::util::JsonParser::ParseFile(path);
			for (const auto& stageJson : root["stages"].GetArray()) {
				StageListEntry entry;
				entry.id            = stageJson["id"].AsString();
				entry.name          = stageJson["name"].AsString();
				entry.stagePath     = stageJson["stage"].AsString();
				entry.thumbnailPath = stageJson["thumbnail"].AsString();
				entries.push_back(entry);
			}
			return entries;
		}
	}
}
