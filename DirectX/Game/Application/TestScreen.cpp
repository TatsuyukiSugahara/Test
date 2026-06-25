#include "stdafx.h"
#include "TestScreen.h"
#include "UI/UIObject.h"
#include "UI/Component/UIAnimationComponent.h"

namespace app
{
	void TestScreen::OnCreate()
	{
		// TestScreen.json で animation セクションが TestImage に付与されており、
		// UIDocumentLoader が UIAnimationComponent を自動生成済み。
		// ここではハンドルだけ取得する。
		imageHandle_ = FindHandle("TestImage");
	}


	void TestScreen::OnEnter()
	{
		animState_ = AnimState::FadeIn;

		auto* obj = Resolve(imageHandle_);
		if (!obj) return;

		auto* anim = obj->GetComponent<aq::ui::UIAnimationComponent>();
		if (!anim) return;

		// 画面表示開始時に FadeIn を再生 (フェードしながら下からスライドイン)
		anim->Play("FadeIn");
	}


	void TestScreen::OnUpdate(float /*dt*/)
	{
		if (animState_ == AnimState::SlideLoop) return;

		auto* obj = Resolve(imageHandle_);
		if (!obj) return;

		auto* anim = obj->GetComponent<aq::ui::UIAnimationComponent>();
		if (!anim) return;

		// FadeIn 完了後に SlideLoop へ遷移
		if (!anim->IsPlaying())
		{
			animState_ = AnimState::SlideLoop;
			anim->Play("SlideLoop");
		}
	}

} // namespace app
