#pragma once
#include "HID/ITouchBackend.h"


namespace aq
{
	namespace hid
	{
		/**
		 * 複数指のダブルタップを拾う検出器
		 *
		 * スマホにはキーボードも中クリックも無いため、デバッグ UI の表示/非表示のような
		 * 「ゲーム操作とは別系統のトグル」を割り当てる先が無い。指を規定本数そろえた
		 * ダブルタップは通常のゲーム操作(仮想スティック 1 本 + ボタン 1 本)と衝突せず、
		 * 誤爆もしにくいのでトグル用に使う。
		 *
		 * **タッチを持たないプラットフォームでも安全に置ける。** `TouchState::count` が
		 * 常に 0 なので条件が成立せず、`Update` は必ず false を返す。呼び出し側に
		 * `#if` を持ち込まないためヘッダのみで実装してある。
		 *
		 * スレッド: `InputManager::Update` と同じスレッドから毎フレーム 1 回呼ぶこと。
		 */
		class MultiTouchDoubleTapDetector
		{
		// ── 定数 ──
		public:
			/** 既定で要求する指の本数 */
			static constexpr uint32_t DEFAULT_REQUIRED_FINGERS = 4;

			/** 1 回のタップとみなす接触時間の上限(秒)。これを超えたら「置いた」扱いで捨てる */
			static constexpr float MAX_TAP_SECONDS = 0.4f;

			/** 1 回目を離してから 2 回目を離すまでの猶予(秒) */
			static constexpr float MAX_INTERVAL_SECONDS = 0.6f;


		// ── メンバ変数 ──
		private:
			/** 成立に必要な指の本数 */
			uint32_t requiredFingers_;

			/** 今の接触中に同時に触れた最大本数(離すと 0 に戻る) */
			uint32_t peakCount_;

			/** 今の接触が始まってからの経過(秒)。接触していなければ 0 */
			float    contactSeconds_;

			/** 1 回目のタップを離してからの経過(秒)。待っていなければ負値 */
			float    sinceFirstTapSeconds_;


		// ── メンバ関数 ──
		public:
			explicit MultiTouchDoubleTapDetector(const uint32_t requiredFingers = DEFAULT_REQUIRED_FINGERS)
				: requiredFingers_(requiredFingers)
				, peakCount_(0)
				, contactSeconds_(0.0f)
				, sinceFirstTapSeconds_(-1.0f)
			{
			}


		public:
			/**
			 * 1 フレーム分の判定を進める
			 *
			 * @param touches   このフレームのタッチ状態
			 * @param deltaTime 前フレームからの経過(秒)
			 * @return ダブルタップが成立したフレームだけ true(成立と同時に内部状態は畳む)
			 */
			bool Update(const TouchState& touches, const float deltaTime)
			{
				// 1 回目の待ち時間を進める。猶予を過ぎたら待つのをやめる。
				if (sinceFirstTapSeconds_ >= 0.0f) {
					sinceFirstTapSeconds_ += deltaTime;
					if (sinceFirstTapSeconds_ > MAX_INTERVAL_SECONDS) {
						sinceFirstTapSeconds_ = -1.0f;
					}
				}

				// 接触中。本数の最大値と接触時間を伸ばすだけ。
				if (touches.count > 0) {
					if (touches.count > peakCount_) {
						peakCount_ = touches.count;
					}
					contactSeconds_ += deltaTime;
					return false;
				}

				// ここから下は「全部離れているフレーム」。
				// 直前まで触れていなければ何もしない(離しっぱなしの間は毎フレーム素通り)。
				if (peakCount_ == 0) {
					return false;
				}

				const bool isTap = (peakCount_ >= requiredFingers_) && (contactSeconds_ <= MAX_TAP_SECONDS);
				peakCount_      = 0;
				contactSeconds_ = 0.0f;

				if (!isTap) {
					// 本数不足や長押しは、待ち状態も含めて捨てる
					// (別の操作をしたのにダブルタップが成立するのを防ぐ)。
					sinceFirstTapSeconds_ = -1.0f;
					return false;
				}

				// 1 回目を待っていたなら成立。そうでなければ自分が 1 回目になる。
				if (sinceFirstTapSeconds_ >= 0.0f) {
					sinceFirstTapSeconds_ = -1.0f;
					return true;
				}
				sinceFirstTapSeconds_ = 0.0f;
				return false;
			}


			/** 要求する指の本数 */
			inline uint32_t GetRequiredFingers() const { return requiredFingers_; }
		};
	}
}
