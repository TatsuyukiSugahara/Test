#include "stdafx.h"
#include "GameInput.h"



namespace app
{
	GameInput* GameInput::sInstance_ = nullptr;


	void GameInput::Initialize()
	{
		if (!sInstance_) sInstance_ = new GameInput();
	}


	void GameInput::Finalize()
	{
		if (sInstance_) { delete sInstance_; sInstance_ = nullptr; }
	}


	GameInput::GameInput()
	{
		SetupMaps();
#if defined(AQ_PLATFORM_UWP)
		// Xbox(UWP)はキーボードが無いのでゲームパッドマップを既定にする
		// (左スティック=移動 / 右スティック=カメラ / D-Pad=移動)。
		UseGamepadMap();
#else
		UseKeyboardMap();
#endif
	}


	void GameInput::SetupMaps()
	{
		using namespace aq::hid;

		keyboardMap_.Bind(GameAction::MoveLeft,     BindKey(KeyBoardType::A));
		keyboardMap_.Bind(GameAction::MoveRight,    BindKey(KeyBoardType::D));
		keyboardMap_.Bind(GameAction::MoveForward,  BindKey(KeyBoardType::W));
		keyboardMap_.Bind(GameAction::MoveBackward, BindKey(KeyBoardType::S));
		keyboardMap_.Bind(GameAction::LookLeft,     BindKey(KeyBoardType::Left));
		keyboardMap_.Bind(GameAction::LookRight,    BindKey(KeyBoardType::Right));
		keyboardMap_.Bind(GameAction::LookUp,       BindKey(KeyBoardType::Up));
		keyboardMap_.Bind(GameAction::LookDown,     BindKey(KeyBoardType::Down));

		gamepadMap_.Bind(GameAction::MoveLeft,     BindPad(PadButton::DLeft));
		gamepadMap_.Bind(GameAction::MoveRight,    BindPad(PadButton::DRight));
		gamepadMap_.Bind(GameAction::MoveForward,  BindPad(PadButton::DUp));
		gamepadMap_.Bind(GameAction::MoveBackward, BindPad(PadButton::DDown));
		gamepadMap_.BindStick(GameAction::Move,    BindLStick());
		gamepadMap_.BindStick(GameAction::Look,    BindRStick());
	}
}
