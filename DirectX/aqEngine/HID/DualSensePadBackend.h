#pragma once
// HID 直読み(SetupAPI / hid.dll)はデスクトップ専用。UWP からは使えないため丸ごと除外する。
#if !defined(AQ_PLATFORM_UWP)
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * DualSense(PS5 パッド)を HID 直読みで扱うバックエンド
		 * XInput エミュレータ(Steam / DS4Windows)に依存せず、入力・振動に加えて
		 * アダプティブトリガー(L2 / R2 の抵抗)まで直接扱う。
		 * USB(input 0x01 / output 0x02)が主対象で、Bluetooth(input 0x31 / output 0x31 + CRC32)は
		 * ベストエフォート実装(実機未確認)。
		 */
		class DualSensePadBackend : public IPadBackend
		{
		public:
			/** 同時に扱える台数。Pad のスロット数に合わせる */
			static constexpr uint32_t MAX_DEVICE_COUNT = 4;


		private:
			/** レポートの最大長。Bluetooth の拡張レポート 0x31 が 78 バイトで最長 */
			static constexpr uint32_t MAX_REPORT_SIZE = 78;

			/** 接続形態。入出力レポートの形が変わる */
			enum class ConnectionType : uint8_t
			{
				Usb,
				Bluetooth,
			};

			/** L2 / R2 の抵抗設定(開始位置 + 一定強度) */
			struct TriggerResistance
			{
				uint8_t startPos = 0;
				uint8_t strength = 0;
			};

			/** 1 台分の接続状態 */
			struct Device
			{
				/** デバイス */
				std::wstring   path;
				HANDLE         handle           = INVALID_HANDLE_VALUE;
				ConnectionType connection       = ConnectionType::Usb;
				uint32_t       inputReportSize  = 0;
				uint32_t       outputReportSize = 0;

				/** 非ブロッキング読み(最新レポートだけを保持する) */
				OVERLAPPED readOverlapped{};
				HANDLE     readEvent   = nullptr;
				bool       readPending = false;
				bool       hasReport   = false;
				uint8_t    readBuffer  [MAX_REPORT_SIZE]{};
				uint8_t    latestReport[MAX_REPORT_SIZE]{};

				/** 非ブロッキング書き。OVERLAPPED とバッファは I/O 完了まで生存が必要なので
				    スタックではなくここに持つ(飛行中は writePending で再利用を防ぐ) */
				OVERLAPPED writeOverlapped{};
				HANDLE     writeEvent   = nullptr;
				bool       writePending = false;
				uint8_t    writeBuffer[MAX_REPORT_SIZE]{};

				/** 出力(振動 / トリガー抵抗)。dirty のときだけ送る */
				uint8_t           rumbleLeft  = 0;
				uint8_t           rumbleRight = 0;
				TriggerResistance leftTrigger;
				TriggerResistance rightTrigger;
				bool              outputDirty = false;
			};


		private:
			Device   devices_[MAX_DEVICE_COUNT];
			uint64_t lastEnumerateTick_ = 0;


		public:
			DualSensePadBackend() = default;
			~DualSensePadBackend() override;

			void Poll                (uint32_t index, PadState& out) override;
			void SetVibration        (uint32_t index, float left, float right) override;
			void SetTriggerResistance(uint32_t index, PadAxis trigger, float startPos, float strength) override;


		private:
			/** デバイスの開閉 */
			void EnumerateDevices();
			bool OpenDevice (Device& device, const wchar_t* path);
			void CloseDevice(Device& device);
			bool IsDeviceOpened(const wchar_t* path) const;

			/** レポートの読み書き */
			void PumpRead         (Device& device);
			void SendOutputReport (Device& device);
			bool ParseInputReport (const Device& device, PadState& out) const;
			void BuildEffectsState(const Device& device, uint8_t* dst) const;

			static void WriteTriggerEffect(const TriggerResistance& resistance, uint8_t* dst);
		};
	}
}
#endif // !AQ_PLATFORM_UWP
