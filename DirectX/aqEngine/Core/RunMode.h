#pragma once
#include <cstdint>


namespace aq
{
	/**
	 * アプリケーションの実行モード。
	 * AppHost が 1 つ持ち、フレームの先頭でだけ切り替える。
	 */
	enum class RunMode : uint8_t
	{
		/** 編集中。ゲームは始まっていない */
		Edit,
		/** 再生中。ゲームのすべての処理が動く */
		Play,
		/** 一時停止中。ゲームの時間が止まり、描画だけが続く */
		Pause,
	};


	/**
	 * System がどのモードで動くかを表すビットの組。
	 */
	enum class RunModeMask : uint8_t
	{
		Edit  = 1,
		Play  = 2,
		Pause = 4,
		All   = 7,
	};


	/** ビットの和集合 */
	constexpr RunModeMask operator|(RunModeMask a, RunModeMask b)
	{
		return static_cast<RunModeMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}


	/** mask が mode を含むか */
	constexpr bool Contains(RunModeMask mask, RunMode mode)
	{
		const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(mode));
		return (static_cast<uint8_t>(mask) & bit) != 0;
	}
}
