#pragma once
// iOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_IOS)
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * UIKit のタッチイベントを受けて、タッチの現在状態を溜めておくバッファ。
		 *
		 * 投入するのは `AqMetalView`(`Platform/iOS/PlatformiOS.mm` の
		 * `touchesBegan/Moved/Ended/Cancelled:withEvent:`)、
		 * 取り出すのは `iOSTouchBackend`。**どちらも iOS 専用コード**なので、
		 * この型は `IPlatform` にも `ITouchBackend` にも露出させない
		 * (Android の `AndroidInputSink` / Mac の `CocoaInputSink` と同じ構造)。
		 *
		 * キーボードとパッドは持たない。iOS のキーボードは `NullKeyboardBackend`、
		 * パッドは `GameControllerPadBackend` が GameController.framework を直接読むため、
		 * ここを経由するのはタッチだけになる(設計書/iOS移植設計.md §5.2 / §5.4)。
		 *
		 * ObjC の型はヘッダに出さない。`UITouch*` は同一性の鍵としてのみ使うので
		 * `const void*` で受ける(下の `touchKey` の説明を参照)。
		 *
		 * スレッド: 投入も取得も**メインスレッド 1 本**から呼ばれる。
		 * `touches*:withEvent:` は UIKit がメインスレッドで配送し、`FetchTouch` を呼ぶ
		 * `InputManager::Update` も `CADisplayLink`(メインランループ)由来のフレーム駆動
		 * (設計書/iOS移植設計.md §3.3)なので、**Android と違ってロックは要らない**。
		 * 将来レンダースレッドや別スレッドのフレーム駆動を入れる場合はこの前提が崩れるので、
		 * そのときは Android と同様に投入/取得の排他を足すこと。
		 */
		class iOSInputSink
		{
		// ── メンバ変数 ──
		private:
			/**
			 * タッチ 1 点の保持。`TouchPoint` と 1 対 1 に対応する。
			 *
			 * `id` が負の間は未使用スロット。指が離れても**次の取得まではスロットを残す**
			 * (`ITouchBackend` の契約が「離した瞬間のフレームだけ pressed = false で 1 回返る」)。
			 */
			struct TouchSlot
			{
				/**
				 * この指を識別する `UITouch*` のアドレス。未使用スロットは nullptr。
				 *
				 * Android は `AMotionEvent` のポインタ ID が整数で届くが、**UIKit が渡すのは
				 * `UITouch*` というオブジェクト**で、整数の識別子は無い。そこで
				 * **アドレスを同一性の鍵として使うだけ**にする。
				 *
				 * **`UITouch` を retain してはいけない**(Apple が明示的に禁止しており、
				 * オブジェクトはシステムが使い回す)。参照は保持せず、逆参照もしない。
				 * began から ended / cancelled までの間は同じ指に同じポインタが渡ることが
				 * 保証されているので、比較だけなら安全に成立する。
				 */
				const void* touchKey = nullptr;

				/**
				 * 上位へ返す識別子。未使用スロットは -1。
				 *
				 * `UITouch*` のアドレスは 64bit で `int32_t` に入らないうえ、
				 * 解放後に別の指へ再利用されうる値なので、そのまま外へ出さない。
				 * **スロットの添字をそのまま id にする**(0..MAX_POINT_COUNT-1)。
				 * スロットは指が離れて取得されるまで解放されないため、同時に触れている
				 * 指の間で一意であり、指が離れるまで値が変わらないという
				 * `TouchPoint::id` の契約を満たす。
				 */
				int32_t id = -1;

				float   x  = 0.0f;
				float   y  = 0.0f;

				/** OS から見て今も触れているか */
				bool down = false;

				/**
				 * 前回の取得以降に押下イベントが来たか。
				 *
				 * タッチは**離散イベント**で届く。1 フレームが長引いた場合
				 * (ロード中のヒッチなど)、そのフレームの中で押して離すところまで進んでしまい、
				 * レベルだけ見ていると押下を取りこぼす。「取得までに一度でも押された」ことを
				 * 憶えておき、その回では押下として返す。次の取得では実レベル(離されていれば
				 * false)に戻るので、タップ判定が 1 回成立する。
				 */
				bool pressedSinceFetch = false;
			};

			/** タッチ点。配列の先頭から詰めずに、touchKey で引いたスロットをそのまま使う */
			TouchSlot touches_[TouchState::MAX_POINT_COUNT]{};


		// ── メンバ関数 ──
		private:
			iOSInputSink() = default;


			/**
			 * 投入側 (AqMetalView の touches*:withEvent:)
			 */
		public:
			/**
			 * 指が触れた。座標はクライアント領域(ビュー)の左上原点・ピクセル。
			 * @param touchKey UITouch* のアドレス。同一性の鍵にするだけで retain も逆参照もしない
			 */
			void OnTouchBegan(const void* touchKey, const float x, const float y);

			/** 指が動いた。未知の鍵なら押下として拾い直す(began の取りこぼし対策) */
			void OnTouchMoved(const void* touchKey, const float x, const float y);

			/** 指が離れた。スロットは次の取得まで残す */
			void OnTouchEnded(const void* touchKey, const float x, const float y);

			/**
			 * ジェスチャが OS に奪われた(ホームへ戻る・通知を引き下ろす等)。
			 * **全点を解放する。**忘れると指が張り付く(Android の `ACTION_CANCEL` と同じ)。
			 * 押下の取りこぼし対策も一緒に落として、幻のタップが成立しないようにする。
			 */
			void OnTouchCancelled();


			/**
			 * 取得側 (iOSTouchBackend)
			 */
		public:
			/**
			 * タッチ状態を書き出す。
			 * 触れている点に加え、**前回の取得以降に一度でも触れた点**も押下として返す。
			 */
			void FetchTouch(TouchState& out);


		private:
			/** 鍵に対応するスロット。無ければ nullptr。create = true なら空きスロットを割り当てる */
			TouchSlot* FindTouchSlot(const void* touchKey, const bool create);


		public:
			static iOSInputSink& Get();
		};
	}
}
#endif // AQ_PLATFORM_IOS
