#include "aq.h"
#include "ProfilerDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <algorithm>
#include <imgui/imgui.h>
#include "RenderConfig.h"


namespace aq
{
	namespace profile
	{
		void ProfilerDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Profiler", nullptr, &show_);
		}


		void ProfilerDebugPanel::DebugRender()
		{
			if (!show_) return;

			if (ImGui::Begin("Profiler", &show_))
				RenderContent();
			ImGui::End();
		}


		void ProfilerDebugPanel::RenderContent()
		{
			// 停止中でなければ毎フレームスナップショットを更新。
			// FrameMs も同時に凍結し、Stop 中は時間軸が動かないようにする。
			if (!paused_)
			{
				Profiler::Get().CaptureSnapshot(snapshot_);
				snapshotFrameMs_ = Profiler::Get().FrameMs();
			}

			if (ImGui::Button(paused_ ? "Resume" : "Stop"))
				paused_ = !paused_;
			ImGui::SameLine();
			ImGui::TextColored(
				paused_ ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
				paused_ ? "停止中 (表示を固定)" : "計測中 (毎フレーム更新)");

			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SliderFloat("Zoom", &zoom_, 1.0f, 50.0f, "%.1fx");

			// カリング系トグル/統計は Rendering タブの "Culling" へ移動 (CullingDebugPanel)。

			ImGui::Separator();

			// --- フレーム総時間 / 同期モード / スレッド別 CPU 時間 ---------------------
			// 各スレッドのルートスコープ合計 (= そのスレッドの 1F あたり実働時間) を求める。
			auto rootSumMs = [](const ThreadFrame& f) {
				double sum = 0.0;
				for (const Sample& s : f.samples)
					if (s.parentIndex < 0) sum += s.durationMs;
				return sum;
			};
			double mainMs = 0.0, renderMs = 0.0;
			for (const ThreadFrame& f : snapshot_)
			{
				if (f.name == "Main")        mainMs   = rootSumMs(f);
				else if (f.name == "Render") renderMs = rootSumMs(f);
			}

			const double frameMs = snapshotFrameMs_;
			const double fps     = (frameMs > 0.0) ? (1000.0 / frameMs) : 0.0;

			// 実フレーム時間 (FlushRender の待ちも含む真の 1F)
			ImGui::Text("Frame:");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.5f, 1.0f), "%.2f ms  (%.1f FPS)", frameMs, fps);

			// 同期モードのバッジ
			ImGui::SameLine();
			ImGui::TextDisabled(" | ");
			ImGui::SameLine();
#ifdef AQ_RENDER_PIPELINED
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "[Async: Render は 1 フレーム遅れ]");
#else
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "[Serial: 毎フレーム完了待ち]");
#endif

			// スレッド別 CPU 時間。直列なら Main+Render≈Frame、非同期なら max(Main,Render)≈Frame。
			const double sumMs = mainMs + renderMs;
			const double maxMs = (mainMs > renderMs) ? mainMs : renderMs;
			ImGui::Text("Main CPU: %.2f ms   Render CPU: %.2f ms   (sum %.2f / max %.2f)",
			            mainMs, renderMs, sumMs, maxMs);
			// 重複の目安: Frame が sum に近い→直列的、max に近い→重複できている。
			if (frameMs > 0.0 && sumMs > 0.0)
			{
				const double overlapPct =
					(sumMs > maxMs) ? ((sumMs - frameMs) / (sumMs - maxMs) * 100.0) : 0.0;
				const double clamped = overlapPct < 0.0 ? 0.0 : (overlapPct > 100.0 ? 100.0 : overlapPct);
				ImGui::SameLine();
				ImGui::TextDisabled("overlap ~%.0f%%", clamped);
			}

			ImGui::Separator();

			// メイン表示: タイムライン (横=時間 / 縦=スレッド)
			RenderTimeline();

			ImGui::Separator();

			// 詳細ツリー (既定で折りたたみ)
			if (ImGui::CollapsingHeader("詳細 (ツリー)"))
			{
				std::vector<const ThreadFrame*> ordered;
				ordered.reserve(snapshot_.size());
				for (const ThreadFrame& f : snapshot_)
					ordered.push_back(&f);
				std::sort(ordered.begin(), ordered.end(),
					[](const ThreadFrame* a, const ThreadFrame* b) { return a->orderIndex < b->orderIndex; });

				for (const ThreadFrame* f : ordered)
					RenderThreadTree(*f);
			}
		}


		namespace
		{
			// スコープ名から安定した淡い色を生成する (FNV-1a ハッシュ)
			ImU32 ColorForName(const char* name)
			{
				uint32_t h = 2166136261u;
				for (const char* p = name; p && *p; ++p)
					h = (h ^ static_cast<uint32_t>(static_cast<unsigned char>(*p))) * 16777619u;
				const float r = 0.35f + 0.55f * ((h         & 0xFF) / 255.0f);
				const float g = 0.35f + 0.55f * (((h >>  8) & 0xFF) / 255.0f);
				const float b = 0.35f + 0.55f * (((h >> 16) & 0xFF) / 255.0f);
				return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.95f));
			}
		}


		void ProfilerDebugPanel::RenderTimeline()
		{
			if (snapshot_.empty())
			{
				ImGui::TextDisabled("(no data)");
				return;
			}

			// 表示順を登録順で安定化
			std::vector<const ThreadFrame*> threads;
			threads.reserve(snapshot_.size());
			for (const ThreadFrame& f : snapshot_)
				threads.push_back(&f);
			std::sort(threads.begin(), threads.end(),
				[](const ThreadFrame* a, const ThreadFrame* b) { return a->orderIndex < b->orderIndex; });

			const double mspt = Profiler::Get().MsPerTick();

			// 全サンプルの共通時間原点と終端を求める
			int64_t originTick = INT64_MAX;
			int64_t maxEndTick = INT64_MIN;
			for (const ThreadFrame* f : threads)
			{
				for (const Sample& s : f->samples)
				{
					originTick = (s.startTick < originTick) ? s.startTick : originTick;
					const int64_t endTick = s.startTick + static_cast<int64_t>(s.durationMs / mspt);
					maxEndTick = (endTick > maxEndTick) ? endTick : maxEndTick;
				}
			}
			if (originTick == INT64_MAX || maxEndTick <= originTick)
			{
				ImGui::TextDisabled("(no samples this frame)");
				return;
			}

			double totalMs = static_cast<double>(maxEndTick - originTick) * mspt;
			// フレーム周期全体を必ず表示する (Render が周期末尾で走るケースを見切れさせない)。
			// 凍結した snapshotFrameMs_ を使い、Stop 中は軸が動かないようにする。
			const double frameMs = snapshotFrameMs_;
			if (frameMs > totalMs) totalMs = frameMs;

			// レイアウト定数
			const float labelW   = 90.0f;
			const float rowH     = 18.0f;
			const float lanePad  = 6.0f;
			const float axisH    = 18.0f;

			// 各スレッドのレーン高さ (最大深度 + 1) を計算
			std::vector<int> laneDepth(threads.size(), 1);
			float contentH = axisH;
			for (size_t i = 0; i < threads.size(); ++i)
			{
				int maxDepth = 0;
				for (const Sample& s : threads[i]->samples)
					maxDepth = (s.depth > maxDepth) ? s.depth : maxDepth;
				laneDepth[i] = maxDepth + 1;
				contentH += laneDepth[i] * rowH + lanePad;
			}

			const float availW       = ImGui::GetContentRegionAvail().x;
			const float timelineW    = (availW - labelW > 50.0f) ? (availW - labelW) : 50.0f;
			const double pxPerMs     = (timelineW / totalMs) * static_cast<double>(zoom_);
			const float  contentW    = labelW + static_cast<float>(totalMs * pxPerMs) + 20.0f;
			const float  childH      = (contentH < 320.0f) ? contentH + 8.0f : 320.0f;

			ImGui::BeginChild("##timeline", ImVec2(0.0f, childH), true,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			// GetCursorScreenPos は水平/垂直スクロールを反映済みのため、
			// 以降の座標計算でスクロール量を再度引く必要はない。
			const ImVec2 p0 = ImGui::GetCursorScreenPos();

			// スクロール領域を確保
			ImGui::Dummy(ImVec2(contentW, contentH));

			const ImU32 gridCol  = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
			const ImU32 labelCol = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.85f));
			const ImU32 textCol  = ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.05f, 1.0f));

			// 時間軸グリッド (1ms ごと、密なら間引く)
			const float timelineX0 = p0.x + labelW;
			{
				double stepMs = 1.0;
				while (stepMs * pxPerMs < 60.0) stepMs *= (stepMs < 5.0 ? 5.0 : 2.0);
				for (double t = 0.0; t <= totalMs; t += stepMs)
				{
					const float x = timelineX0 + static_cast<float>(t * pxPerMs);
					dl->AddLine(ImVec2(x, p0.y + axisH), ImVec2(x, p0.y + contentH), gridCol);
					char lbl[32];
					snprintf(lbl, sizeof(lbl), "%.2fms", t);
					dl->AddText(ImVec2(x + 2.0f, p0.y + 2.0f), labelCol, lbl);
				}

				// フレーム周期の終端マーカー (この線までが 1 フレーム)。
				// Main(周期頭) と Render の位置関係が「同じ 1 フレーム内のどこか」を読み取れる。
				if (frameMs > 0.0)
				{
					const float fx = timelineX0 + static_cast<float>(frameMs * pxPerMs);
					dl->AddLine(ImVec2(fx, p0.y + axisH), ImVec2(fx, p0.y + contentH),
					            ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.3f, 0.6f)), 2.0f);
					dl->AddText(ImVec2(fx - 64.0f, p0.y + 2.0f),
					            ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.3f, 0.9f)), "frame end");
				}
			}

			// 各スレッドのレーンを描画
			const ImVec2 mouse = ImGui::GetMousePos();
			const bool   hovered = ImGui::IsWindowHovered();
			const Sample* tipSample   = nullptr;
			const ThreadFrame* tipThread = nullptr;
			double tipStartMs = 0.0;

			float laneY = p0.y + axisH;
			for (size_t i = 0; i < threads.size(); ++i)
			{
				const ThreadFrame* f = threads[i];
				const float laneH = laneDepth[i] * rowH;

				// このレーンの実働時間 (ルートスコープ合計)
				double laneSumMs = 0.0;
				for (const Sample& s : f->samples)
					if (s.parentIndex < 0) laneSumMs += s.durationMs;

				const bool isRender = (f->name == "Render");
				(void)isRender;  // 直列ビルドでは未使用
#ifdef AQ_RENDER_PIPELINED
				// 非同期: Render レーンは「1 フレーム遅れで Main と重複実行」していることを
				// 帯の背景色で明示する。Main のバーと時間軸上で重なって見えるのが正常。
				if (isRender)
				{
					dl->AddRectFilled(
						ImVec2(p0.x, laneY), ImVec2(p0.x + contentW, laneY + laneH),
						ImGui::GetColorU32(ImVec4(0.25f, 0.55f, 0.75f, 0.12f)));
				}
#endif

				// レーン名 (1 行目) + 実働時間 (2 行目)。背の低いレーンは 1 行目だけ。
				dl->AddText(ImVec2(p0.x + 2.0f, laneY + 1.0f), labelCol, f->name.c_str());
				char laneInfo[48];
#ifdef AQ_RENDER_PIPELINED
				if (isRender)
					snprintf(laneInfo, sizeof(laneInfo), "%.2fms N-1", laneSumMs);
				else
					snprintf(laneInfo, sizeof(laneInfo), "%.2fms", laneSumMs);
#else
				snprintf(laneInfo, sizeof(laneInfo), "%.2fms", laneSumMs);
#endif
				// 背の高いレーン (Main / Render) のみ 2 行目に実働時間を表示。
				// 背の低いワーカーは名前と重なるため省略 (合計はヘッダの Main/Render CPU 参照)。
				if (laneH >= rowH * 2.0f - 2.0f)
				{
					const ImU32 infoCol = ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.85f, 0.75f));
					dl->AddText(ImVec2(p0.x + 2.0f, laneY + rowH), infoCol, laneInfo);
				}

				for (const Sample& s : f->samples)
				{
					const double startMs = static_cast<double>(s.startTick - originTick) * mspt;
					const float x = timelineX0 + static_cast<float>(startMs * pxPerMs);
					float w = static_cast<float>(s.durationMs * pxPerMs);
					if (w < 1.0f) w = 1.0f;
					const float y = laneY + s.depth * rowH;

					// クリップ: ラベル列より左は描かない
					float drawX = x;
					float drawW = w;
					if (drawX < timelineX0) { drawW -= (timelineX0 - drawX); drawX = timelineX0; }
					if (drawW <= 0.0f) continue;

					const ImVec2 a(drawX, y + 1.0f);
					const ImVec2 b(drawX + drawW, y + rowH - 1.0f);
					dl->AddRectFilled(a, b, ColorForName(s.name), 2.0f);
					dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.4f)), 2.0f);

					// バー内ラベル (幅が足りる場合のみ)
					if (drawW > 28.0f)
					{
						ImGui::PushClipRect(a, b, true);
						dl->AddText(ImVec2(drawX + 3.0f, y + 2.0f), textCol, s.name);
						ImGui::PopClipRect();
					}

					// ホバー判定
					if (hovered &&
					    mouse.x >= a.x && mouse.x <= b.x &&
					    mouse.y >= a.y && mouse.y <= b.y)
					{
						tipSample  = &s;
						tipThread  = f;
						tipStartMs = startMs;
					}
				}

				laneY += laneDepth[i] * rowH + lanePad;
			}

			// ツールチップ
			if (tipSample)
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s", tipSample->name);
				ImGui::Separator();
				ImGui::Text("Thread : %s", tipThread->name.c_str());
				ImGui::Text("Time   : %.4f ms", tipSample->durationMs);
				ImGui::Text("Start  : +%.4f ms", tipStartMs);
				ImGui::Text("Depth  : %d", tipSample->depth);
				ImGui::EndTooltip();
			}

			ImGui::EndChild();
		}


		void ProfilerDebugPanel::RenderThreadTree(const ThreadFrame& frame)
		{
			// スレッドの総時間 (ルートスコープの合計)
			double threadTotalMs = 0.0;
			for (const Sample& s : frame.samples)
				if (s.parentIndex < 0) threadTotalMs += s.durationMs;

			char header[160];
			snprintf(header, sizeof(header), "%s  (%.3f ms, %zu scopes)###thr_%llu",
			         frame.name.c_str(), threadTotalMs, frame.samples.size(),
			         static_cast<unsigned long long>(frame.threadId));

			if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
				return;

			if (frame.samples.empty())
			{
				ImGui::TextDisabled("  (no samples)");
				return;
			}

			// 親 → 子 隣接リストを構築
			std::vector<std::vector<int>> children(frame.samples.size());
			std::vector<int> roots;
			for (int i = 0; i < static_cast<int>(frame.samples.size()); ++i)
			{
				const int parent = frame.samples[i].parentIndex;
				if (parent < 0) roots.push_back(i);
				else if (parent < static_cast<int>(children.size())) children[parent].push_back(i);
			}

			const ImGuiTableFlags tableFlags =
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable     | ImGuiTableFlags_SizingStretchProp;

			const std::string tableId = "##tbl_" + std::to_string(frame.threadId);
			if (ImGui::BeginTable(tableId.c_str(), 3, tableFlags))
			{
				ImGui::TableSetupColumn("Scope",    ImGuiTableColumnFlags_WidthStretch, 0.6f);
				ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthStretch, 0.25f);
				ImGui::TableSetupColumn("%",         ImGuiTableColumnFlags_WidthStretch, 0.15f);
				ImGui::TableHeadersRow();

				for (int root : roots)
					RenderNode(frame, children, root, threadTotalMs);

				ImGui::EndTable();
			}
		}


		void ProfilerDebugPanel::RenderNode(const ThreadFrame& frame,
		                                    const std::vector<std::vector<int>>& children,
		                                    int index, double threadTotalMs)
		{
			const Sample& s        = frame.samples[index];
			const bool    hasChild = !children[index].empty();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGuiTreeNodeFlags flags =
				ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
			if (!hasChild)
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet |
				         ImGuiTreeNodeFlags_NoTreePushOnOpen;

			// index を ID に含めて同名スコープを区別する
			ImGui::PushID(index);
			const bool open = ImGui::TreeNodeEx(s.name, flags);

			ImGui::TableNextColumn();
			ImGui::Text("%.3f", s.durationMs);

			ImGui::TableNextColumn();
			const double pct = (threadTotalMs > 0.0) ? (s.durationMs / threadTotalMs * 100.0) : 0.0;
			ImGui::Text("%.1f%%", pct);

			if (open && hasChild)
			{
				for (int child : children[index])
					RenderNode(frame, children, child, threadTotalMs);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
}
#endif
