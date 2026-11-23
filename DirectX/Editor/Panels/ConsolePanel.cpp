#include "stdafx.h"
#include "ConsolePanel.h"
#include <cstdio>
#include <string>
#include <utility>


namespace editor
{
	namespace
	{
		/** 重要度ごとの文字色 */
		static constexpr ImVec4 WARNING_TEXT_COLOR = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
		static constexpr ImVec4 ERROR_TEXT_COLOR   = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

		/** 浮かせて出すときの大きさ(メインビューポートの作業領域に対する割合) */
		static constexpr float FLOATING_WIDTH_RATIO  = 0.60f;
		static constexpr float FLOATING_HEIGHT_RATIO = 0.25f;

		/** 文字の絞り込みの入力欄の幅 */
		static constexpr float FILTER_INPUT_WIDTH = 200.0f;
	}


	void ConsolePanel::Draw(ConsoleLog& log, const bool placeFloatingIfNew)
	{
		// 閉じている間のログも、開いたときに見られるように写しておく
		PullNewLines(log);
		if (!isOpen_) return;

		// 配置の保存が無いときだけ効く(ImGuiCond_FirstUseEver)。ドッキングすれば以後は ini に残る
		if (placeFloatingIfNew) {
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y),
			                        ImGuiCond_FirstUseEver, ImVec2(0.5f, 1.0f));
			ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * FLOATING_WIDTH_RATIO, viewport->WorkSize.y * FLOATING_HEIGHT_RATIO),
			                         ImGuiCond_FirstUseEver);
		}

		if (ImGui::Begin(WINDOW_NAME, &isOpen_)) {
			DrawToolbar(log);
			ImGui::Separator();
			DrawLines();
		}
		ImGui::End();
	}


	void ConsolePanel::PullNewLines(const ConsoleLog& log)
	{
		std::vector<ConsoleLine> newLines;
		lastSerial_ = log.CopySince(lastSerial_, newLines);
		for (ConsoleLine& line : newLines) {
			lines_.push_back(std::move(line));
		}
		while (lines_.size() > ConsoleLog::MAX_LINES) {
			lines_.pop_front();
		}
	}


	void ConsolePanel::DrawToolbar(ConsoleLog& log)
	{
		// ImGui のフォントは ASCII のみを読み込んでいるので英語で出す
		if (ImGui::Button("Clear")) {
			log.Clear();
			lines_.clear();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &autoScroll_);
		ImGui::SameLine();
		if (ImGui::Button("Copy")) {
			CopyVisibleToClipboard();
		}

		// 件数は絞り込みに依らず、写しにある行の数
		size_t infoCount    = 0;
		size_t warningCount = 0;
		size_t errorCount   = 0;
		for (const ConsoleLine& line : lines_) {
			switch (line.severity) {
			case LogSeverity::Info:    ++infoCount;    break;
			case LogSeverity::Warning: ++warningCount; break;
			case LogSeverity::Error:   ++errorCount;   break;
			}
		}

		// 件数が変わってもチェックボックスの ID が変わらないように、### 以降で固定する
		char label[64];
		ImGui::SameLine();
		std::snprintf(label, sizeof(label), "Info (%zu)###Info", infoCount);
		ImGui::Checkbox(label, &showInfo_);
		ImGui::SameLine();
		std::snprintf(label, sizeof(label), "Warning (%zu)###Warning", warningCount);
		ImGui::Checkbox(label, &showWarning_);
		ImGui::SameLine();
		std::snprintf(label, sizeof(label), "Error (%zu)###Error", errorCount);
		ImGui::Checkbox(label, &showError_);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(FILTER_INPUT_WIDTH);
		filter_.Draw("Filter");
	}


	void ConsolePanel::DrawLines()
	{
		visibleRows_.clear();
		for (size_t i = 0; i < lines_.size(); ++i) {
			if (IsVisible(lines_[i])) {
				visibleRows_.push_back(static_cast<int>(i));
			}
		}

		if (ImGui::BeginChild("##ConsoleLines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(visibleRows_.size()));
			while (clipper.Step()) {
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
					const ConsoleLine& line = lines_[static_cast<size_t>(visibleRows_[static_cast<size_t>(row)])];

					bool hasColor = true;
					switch (line.severity) {
					case LogSeverity::Warning: ImGui::PushStyleColor(ImGuiCol_Text, WARNING_TEXT_COLOR); break;
					case LogSeverity::Error:   ImGui::PushStyleColor(ImGuiCol_Text, ERROR_TEXT_COLOR);   break;
					default:                   hasColor = false;                                         break;
					}
					ImGui::TextUnformatted(line.text.c_str(), line.text.c_str() + line.text.size());
					if (hasColor) {
						ImGui::PopStyleColor();
					}
				}
			}

			// 一番下を見ていたときだけ下へ送る(上へ戻して読んでいる間は動かさない)
			if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
				ImGui::SetScrollHereY(1.0f);
			}
		}
		ImGui::EndChild();
	}


	bool ConsolePanel::IsVisible(const ConsoleLine& line) const
	{
		switch (line.severity) {
		case LogSeverity::Info:    if (!showInfo_)    return false; break;
		case LogSeverity::Warning: if (!showWarning_) return false; break;
		case LogSeverity::Error:   if (!showError_)   return false; break;
		}
		return filter_.PassFilter(line.text.c_str(), line.text.c_str() + line.text.size());
	}


	void ConsolePanel::CopyVisibleToClipboard() const
	{
		std::string text;
		for (const ConsoleLine& line : lines_) {
			if (!IsVisible(line)) continue;
			text += line.text;
			text += '\n';
		}
		ImGui::SetClipboardText(text.c_str());
	}
}
