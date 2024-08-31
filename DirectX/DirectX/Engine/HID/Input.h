#pragma once


namespace engine
{
	namespace hid
	{
		/**
		 * キーボード入力種類
		 */
		enum KeyBoardType
		{
			BUTTON_LEFT		= DIK_LEFT,
			BUTTON_RIGHT	= DIK_RIGHT,
			BUTTON_UP		= DIK_UP,
			BUTTON_DOWN		= DIK_DOWN,

			BUTTON_W		= DIK_W,
			BUTTON_A		= DIK_A,
			BUTTON_S		= DIK_S,
			BUTTON_D		= DIK_D,

			BUTTON_SPACE	= DIK_SPACE,
		};


		/**
		 * キーボード入力
		 */
		class KeyBoard
		{
		private:
			static constexpr uint16_t MAXIMUM_COUNT = 256;


		private:
			LPDIRECTINPUTDEVICE8 device_;
			uint8_t now_[MAXIMUM_COUNT];
			uint8_t old_[MAXIMUM_COUNT];


		public:
			KeyBoard();
			~KeyBoard();

			HRESULT Initialize(LPDIRECTINPUT8 input);
			void Update();


		public:
			/** 押したフレームだけTrue */
			bool IsTrigger(const uint32_t key) const;
			/** 押している */
			bool IsPressed(const uint32_t key) const;
		};




		/*******************************************/


		enum MouseType
		{
			CLICK_L = 0,
			CLICK_R,
			CLICK_WHEEL,
			
			X = 9,
			Y,

			ROLL_WHEEL,
		};


		class Mouse
		{
		private:
			LPDIRECTINPUTDEVICE8 device_;
			DIMOUSESTATE2 mouseState_;


		public:
			Mouse();
			~Mouse();

			HRESULT Initialize(LPDIRECTINPUT8 input, HWND hWnd);
			void Update();
			int32_t GetValue(MouseType type) const;
			
			engine::math::Vector2 GetCursorPositionOnScreen() const;
			engine::math::Vector2 GetCursorPositionOnWindow(HWND hwnd) const;
		};



		/*******************************************/


		/**
		 * 入力管理
		 */
		class InputManager
		{
		private:
			LPDIRECTINPUT8 input_;
			KeyBoard* keyBoard_;
			Mouse* mouse_;


		public:
			InputManager();
			~InputManager();

			HRESULT Setup();
			void Update();
			KeyBoard& GetKeyBoard() { return *keyBoard_; }


		private:
			static InputManager* sInstance_;


		public:
			static void Initialize()
			{
				if (sInstance_ == nullptr) {
					sInstance_ = new InputManager();
				}
			}
			static InputManager& Get() { return *sInstance_; }
			static void Finalize()
			{
				if (sInstance_) {
					delete sInstance_;
					sInstance_ = nullptr;
				}
			}
		};
	}
}