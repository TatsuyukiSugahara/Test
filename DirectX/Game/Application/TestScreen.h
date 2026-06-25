#pragma once

namespace app
{
	// UIAnimation テスト用スクリーン。
	// OnEnter() で "FadeIn" を再生し、完了後に "SlideLoop" へ自動遷移する。
	class TestScreen : public aq::ui::UIScreen
	{
	public:
		void OnCreate()          override;
		void OnEnter()           override;
		void OnUpdate(float dt)  override;

	private:
		aq::ui::UIObjectHandle imageHandle_;

		enum class AnimState { FadeIn, SlideLoop };
		AnimState animState_ = AnimState::FadeIn;
	};

} // namespace app
