#pragma once
#include <memory>
#include <vector>
#include "HID/IPadBackend.h"


namespace aq
{
	namespace hid
	{
		/**
		 * 複数のパッドバックエンドを束ね、1 つのパッドとして見せる合成バックエンド
		 * ボタンは OR、軸は絶対値の大きい方を採り、いずれかが接続していれば接続扱い。
		 * 「最後に入力があった方を使う」方式は状態を持つぶん挙動が読みにくくなるため採らない。
		 * Android では物理コントローラと仮想パッドを束ねるのに使うが、
		 * この型自体はプラットフォーム非依存。
		 */
		class CompositePadBackend : public IPadBackend
		{
		private:
			/** 束ねたバックエンド(所有。Add した順に合成する) */
			std::vector<std::unique_ptr<IPadBackend>> backends_;


		public:
			CompositePadBackend();


		public:
			/**
			 * バックエンドを追加し、所有権を受け取る
			 * @param backend 追加するバックエンド(nullptr は無視する)
			 */
			void Add(std::unique_ptr<IPadBackend> backend);


		public:
			void Poll                (uint32_t index, PadState& out) override;
			void SetVibration        (uint32_t index, float left, float right) override;
			void SetTriggerResistance(uint32_t index, PadAxis trigger, float startPos, float strength) override;
		};
	}
}
