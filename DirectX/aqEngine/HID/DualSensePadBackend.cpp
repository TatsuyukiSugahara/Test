#include "aq.h"
// HID 直読みは Win32 デスクトップ専用。UWP(Xbox)ではパッドは WinRTGamepadBackend が担当するため空 TU。
#if defined(AQ_PLATFORM_WIN32)
#include "HID/DualSensePadBackend.h"
#include <setupapi.h>
#include <hidsdi.h>

namespace aq
{
	namespace hid
	{
		namespace
		{
			/** 対象デバイス(Sony / DualSense・DualSense Edge) */
			static constexpr uint16_t SONY_VENDOR_ID          = 0x054C;
			static constexpr uint16_t DUALSENSE_PRODUCT_ID    = 0x0CE6;
			static constexpr uint16_t DUALSENSE_EDGE_PRODUCT_ID = 0x0DF2;

			/** レポート長 */
			static constexpr uint32_t USB_INPUT_REPORT_SIZE  = 64;
			static constexpr uint32_t BT_INPUT_REPORT_SIZE   = 78;
			static constexpr uint32_t USB_OUTPUT_REPORT_SIZE = 48;
			static constexpr uint32_t BT_OUTPUT_REPORT_SIZE  = 78;
			static constexpr uint32_t BT_FEATURE_REPORT_SIZE = 41;
			static constexpr uint32_t TRIGGER_EFFECT_SIZE    = 11;

			/** レポート ID */
			static constexpr uint8_t REPORT_ID_SIMPLE   = 0x01;   // USB の通常入力 / BT の簡易入力
			static constexpr uint8_t REPORT_ID_EXTENDED = 0x31;   // BT の拡張入力・出力
			static constexpr uint8_t REPORT_ID_USB_OUT  = 0x02;   // USB の出力
			static constexpr uint8_t REPORT_ID_BT_CALIB = 0x05;   // BT を拡張モードへ移行させる feature report

			/** Bluetooth 出力レポート末尾の CRC32 に使う種バイト */
			static constexpr uint8_t  BT_CRC_SEED       = 0xA2;
			static constexpr uint32_t BT_CRC_OFFSET     = 74;     // CRC32 を書き込む位置(= CRC 対象バイト数)

			/** ポーリング挙動 */
			static constexpr uint64_t ENUMERATE_INTERVAL_MS = 1000;   // 抜き差し追従の再列挙間隔
			static constexpr uint32_t MAX_READ_PER_POLL     = 8;      // 溜まったレポートを捨てて最新へ追いつく上限
			static constexpr uint32_t WRITE_TIMEOUT_MS      = 10;

			/** スティック正規化。中心 128、遊び幅は XInput の左スティックと同じ割合に合わせる */
			static constexpr float STICK_CENTER    = 128.0f;
			static constexpr float STICK_HALFRANGE = 127.5f;
			static constexpr float STICK_DEADZONE  = 0.24f;

			/** ボタンビット */
			static constexpr uint8_t FACE_SQUARE   = 0x10;
			static constexpr uint8_t FACE_CROSS    = 0x20;
			static constexpr uint8_t FACE_CIRCLE   = 0x40;
			static constexpr uint8_t FACE_TRIANGLE = 0x80;
			static constexpr uint8_t DPAD_NEUTRAL  = 8;

			/** 十字キーのハット値(0=上, 時計回り, 8=中立)→ 上下左右ビット */
			static constexpr uint8_t DPAD_UP    = 0x01;
			static constexpr uint8_t DPAD_DOWN  = 0x02;
			static constexpr uint8_t DPAD_LEFT  = 0x04;
			static constexpr uint8_t DPAD_RIGHT = 0x08;
			static const uint8_t HAT_TO_DPAD[9] =
			{
				DPAD_UP,
				DPAD_UP   | DPAD_RIGHT,
				DPAD_RIGHT,
				DPAD_DOWN | DPAD_RIGHT,
				DPAD_DOWN,
				DPAD_DOWN | DPAD_LEFT,
				DPAD_LEFT,
				DPAD_UP   | DPAD_LEFT,
				0,
			};


			/** input report 内のバイト位置。接続形態とレポート ID で先頭がずれる */
			struct InputLayout
			{
				uint32_t stick;      // LX, LY, RX, RY
				uint32_t trigger;    // L2, R2
				uint32_t faceDpad;   // 下位 4bit = 十字キー / 上位 4bit = □ × ○ △
				uint32_t shoulder;   // L1 R1 L2 R2 Create Options L3 R3
			};


			// クランプ付きの 0..255 変換([0, 1] → バイト)。
			uint8_t ToByte(const float value)
			{
				return static_cast<uint8_t>(math::Clamp01(value) * 255.0f + 0.5f);
			}


			// スティックを [-1, 1] へ正規化する。デッドゾーンの外側を線形に引き伸ばす流儀は
			// XInputPadBackend::NormalizeAxis に合わせ、XInput パッドと操作感を揃える。
			float NormalizeStick(const uint8_t raw, const bool invert)
			{
				float value = (static_cast<float>(raw) - STICK_CENTER) / STICK_HALFRANGE;
				if (invert) { value = -value; }

				if (value > -STICK_DEADZONE && value < STICK_DEADZONE) { return 0.0f; }

				float result;
				if (value > 0.0f)
					result = (value - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
				else
					result = (value + STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
				// raw = 0 側が中心から 128 離れており、わずかに -1 を下回るためクランプする
				return result < -1.0f ? -1.0f : (result > 1.0f ? 1.0f : result);
			}


			// Bluetooth の出力レポートに付ける CRC32。0xA2 を先頭に加えた列に対して計算する
			// (DS5W: CRC32::compute)。テーブルは aq::util の CRC32 実装を流用する。
			uint32_t ComputeReportCrc32(const uint8_t* data, const uint32_t size)
			{
				uint32_t crc = 0xFFFFFFFF;
				crc = (crc >> 8) ^ aq::util::_internal::CRC32_TABLE[(crc ^ BT_CRC_SEED) & 0xFF];
				for (uint32_t i = 0; i < size; ++i)
				{
					crc = (crc >> 8) ^ aq::util::_internal::CRC32_TABLE[(crc ^ data[i]) & 0xFF];
				}
				return crc ^ 0xFFFFFFFF;
			}


			// HID デバイスパスが DualSense のものかを VID / PID で判定する。
			// 排他オープンされていても属性は読めるよう、アクセス権 0 で開く。
			bool IsDualSenseDevice(const wchar_t* path)
			{
				HANDLE handle = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
				                            nullptr, OPEN_EXISTING, 0, nullptr);
				if (handle == INVALID_HANDLE_VALUE) { return false; }

				HIDD_ATTRIBUTES attributes{};
				attributes.Size = sizeof(attributes);
				const bool ok = (HidD_GetAttributes(handle, &attributes) == TRUE);
				CloseHandle(handle);

				if (!ok || attributes.VendorID != SONY_VENDOR_ID) { return false; }
				return attributes.ProductID == DUALSENSE_PRODUCT_ID
				    || attributes.ProductID == DUALSENSE_EDGE_PRODUCT_ID;
			}
		}


		DualSensePadBackend::~DualSensePadBackend()
		{
			for (auto& device : devices_)
			{
				CloseDevice(device);
			}
		}


		void DualSensePadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};
			if (index >= MAX_DEVICE_COUNT) { return; }

			// 抜き差しへの追従。毎フレーム列挙すると重いので 1 秒間隔に絞る
			// (Poll は 1 フレームにスロット数だけ呼ばれるが、時刻で見ているので実際の列挙は 1 回)。
			const uint64_t tick = GetTickCount64();
			if (tick - lastEnumerateTick_ >= ENUMERATE_INTERVAL_MS)
			{
				lastEnumerateTick_ = tick;
				EnumerateDevices();
			}

			Device& device = devices_[index];
			if (device.handle == INVALID_HANDLE_VALUE) { return; }

			PumpRead(device);
			SendOutputReport(device);

			// 読み書きに失敗するとここまでに切断扱いでクローズされている
			if (device.handle == INVALID_HANDLE_VALUE || !device.hasReport) { return; }

			out.connected = ParseInputReport(device, out);
		}


		void DualSensePadBackend::SetVibration(uint32_t index, float left, float right)
		{
			if (index >= MAX_DEVICE_COUNT) { return; }

			Device& device = devices_[index];
			if (device.handle == INVALID_HANDLE_VALUE) { return; }

			const uint8_t l = ToByte(left);
			const uint8_t r = ToByte(right);
			if (device.rumbleLeft == l && device.rumbleRight == r) { return; }

			device.rumbleLeft  = l;
			device.rumbleRight = r;
			device.outputDirty = true;
		}


		void DualSensePadBackend::SetTriggerResistance(uint32_t index, PadAxis trigger, float startPos, float strength)
		{
			if (index >= MAX_DEVICE_COUNT) { return; }
			if (trigger != PadAxis::LTrigger && trigger != PadAxis::RTrigger) { return; }

			Device& device = devices_[index];
			if (device.handle == INVALID_HANDLE_VALUE) { return; }

			TriggerResistance& target = (trigger == PadAxis::LTrigger) ? device.leftTrigger : device.rightTrigger;
			const uint8_t start = ToByte(startPos);
			const uint8_t force = ToByte(strength);
			if (target.startPos == start && target.strength == force) { return; }

			target.startPos    = start;
			target.strength    = force;
			device.outputDirty = true;
		}


		void DualSensePadBackend::EnumerateDevices()
		{
			GUID hidGuid;
			HidD_GetHidGuid(&hidGuid);

			HDEVINFO deviceInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
			                                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
			if (deviceInfo == INVALID_HANDLE_VALUE) { return; }

			SP_DEVICE_INTERFACE_DATA interfaceData{};
			interfaceData.cbSize = sizeof(interfaceData);

			std::vector<uint8_t> detailBuffer;
			for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &hidGuid, i, &interfaceData); ++i)
			{
				DWORD required = 0;
				SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &required, nullptr);
				if (required == 0) { continue; }

				detailBuffer.resize(required);
				auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
				detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
				if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail, required, nullptr, nullptr))
				{
					continue;
				}

				if (IsDeviceOpened(detail->DevicePath))  { continue; }
				if (!IsDualSenseDevice(detail->DevicePath)) { continue; }

				// index は列挙順に割り当てる。既に開いているスロットはそのまま残すので、
				// 抜き差ししても残りのパッドのスロットは動かない。
				for (uint32_t slot = 0; slot < MAX_DEVICE_COUNT; ++slot)
				{
					if (devices_[slot].handle == INVALID_HANDLE_VALUE)
					{
						OpenDevice(devices_[slot], detail->DevicePath);
						break;
					}
				}
			}

			SetupDiDestroyDeviceInfoList(deviceInfo);
		}


		bool DualSensePadBackend::OpenDevice(Device& device, const wchar_t* path)
		{
			HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
			                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (handle == INVALID_HANDLE_VALUE) { return false; }

			// 入力レポート長で接続形態を見分ける(USB 64 バイト / Bluetooth 78 バイト)。
			// 出力レポート長も控えておく。Windows の HID 書き込みは、
			// 実データが短くてもデバイスが申告した長さちょうどで送る必要がある。
			uint32_t inputSize  = 0;
			uint32_t outputSize = 0;
			PHIDP_PREPARSED_DATA preparsed = nullptr;
			if (HidD_GetPreparsedData(handle, &preparsed))
			{
				HIDP_CAPS caps{};
				if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
				{
					inputSize  = caps.InputReportByteLength;
					outputSize = caps.OutputReportByteLength;
				}
				HidD_FreePreparsedData(preparsed);
			}
			if (inputSize == 0 || inputSize > MAX_REPORT_SIZE || outputSize > MAX_REPORT_SIZE)
			{
				CloseHandle(handle);
				return false;
			}

			device.path             = path;
			device.handle           = handle;
			device.inputReportSize  = inputSize;
			device.outputReportSize = outputSize;
			device.connection       = (inputSize >= BT_INPUT_REPORT_SIZE) ? ConnectionType::Bluetooth : ConnectionType::Usb;
			device.readEvent        = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			device.writeEvent       = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			device.readPending      = false;
			device.hasReport        = false;

			// Bluetooth は feature report 0x05(キャリブレーション)を一度読むと、
			// 入力が簡易 0x01 から拡張 0x31 へ切り替わり、出力レポートも受け付けるようになる。
			// USB では不要。この経路は実機未確認(ベストエフォート)。
			if (device.connection == ConnectionType::Bluetooth)
			{
				uint8_t feature[BT_FEATURE_REPORT_SIZE]{};
				feature[0] = REPORT_ID_BT_CALIB;
				HidD_GetFeature(handle, feature, sizeof(feature));
			}
			return true;
		}


		void DualSensePadBackend::CloseDevice(Device& device)
		{
			if (device.handle != INVALID_HANDLE_VALUE)
			{
				if (device.readPending) { CancelIoEx(device.handle, &device.readOverlapped); }
				CloseHandle(device.handle);
			}
			if (device.readEvent)  { CloseHandle(device.readEvent);  }
			if (device.writeEvent) { CloseHandle(device.writeEvent); }

			// 空きスロットへ戻す。次の再列挙で拾い直される
			device = Device{};
		}


		bool DualSensePadBackend::IsDeviceOpened(const wchar_t* path) const
		{
			for (const auto& device : devices_)
			{
				if (device.handle != INVALID_HANDLE_VALUE && device.path == path) { return true; }
			}
			return false;
		}


		void DualSensePadBackend::PumpRead(Device& device)
		{
			// 溜まったレポートを読み捨てて最新だけを残す。overlapped なので
			// 未着なら即座に抜け、ゲームループを待たせない。
			for (uint32_t i = 0; i < MAX_READ_PER_POLL; ++i)
			{
				if (!device.readPending)
				{
					ResetEvent(device.readEvent);
					device.readOverlapped        = {};
					device.readOverlapped.hEvent = device.readEvent;

					DWORD read = 0;
					if (!ReadFile(device.handle, device.readBuffer, device.inputReportSize, &read, &device.readOverlapped))
					{
						if (GetLastError() != ERROR_IO_PENDING)
						{
							CloseDevice(device);
							return;
						}
						device.readPending = true;
					}
				}

				DWORD transferred = 0;
				if (!GetOverlappedResult(device.handle, &device.readOverlapped, &transferred, FALSE))
				{
					// まだ届いていないだけなら次フレームに持ち越す
					if (GetLastError() == ERROR_IO_INCOMPLETE) { return; }
					CloseDevice(device);
					return;
				}

				device.readPending = false;
				if (transferred > 0)
				{
					aq::memory::Copy(device.latestReport, device.readBuffer, static_cast<uint32_t>(transferred));
					device.hasReport = true;
				}
			}
		}


		void DualSensePadBackend::SendOutputReport(Device& device)
		{
			if (device.handle == INVALID_HANDLE_VALUE || !device.outputDirty) { return; }

			uint8_t  buffer[BT_OUTPUT_REPORT_SIZE]{};
			uint32_t size = 0;
			if (device.connection == ConnectionType::Usb)
			{
				buffer[0] = REPORT_ID_USB_OUT;
				BuildEffectsState(device, &buffer[1]);
				size = USB_OUTPUT_REPORT_SIZE;
			}
			else
			{
				// Bluetooth はレポート ID の後にシーケンス / タグの 1 バイトが入り、
				// 末尾 4 バイトが CRC32(DS5W の BT 出力と同じ並び)。実機未確認。
				buffer[0] = REPORT_ID_EXTENDED;
				buffer[1] = 0x02;
				BuildEffectsState(device, &buffer[2]);

				const uint32_t crc = ComputeReportCrc32(buffer, BT_CRC_OFFSET);
				buffer[BT_CRC_OFFSET + 0] = static_cast<uint8_t>(crc         & 0xFF);
				buffer[BT_CRC_OFFSET + 1] = static_cast<uint8_t>((crc >>  8) & 0xFF);
				buffer[BT_CRC_OFFSET + 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
				buffer[BT_CRC_OFFSET + 3] = static_cast<uint8_t>((crc >> 24) & 0xFF);
				size = BT_OUTPUT_REPORT_SIZE;
			}

			// デバイスが申告した長さがあればそちらを優先する(残りは 0 のまま送る)。
			if (device.outputReportSize > size) { size = device.outputReportSize; }

			ResetEvent(device.writeEvent);
			OVERLAPPED overlapped{};
			overlapped.hEvent = device.writeEvent;

			DWORD written = 0;
			if (!WriteFile(device.handle, buffer, size, &written, &overlapped))
			{
				if (GetLastError() != ERROR_IO_PENDING)
				{
					CloseDevice(device);
					return;
				}
				// 数十バイトの書き込みなので通常は即完了する。抜かれた直後などで
				// 返ってこないときはキャンセルして切断扱いにし、ゲームループを止めない。
				if (WaitForSingleObject(device.writeEvent, WRITE_TIMEOUT_MS) != WAIT_OBJECT_0)
				{
					CancelIoEx(device.handle, &overlapped);
					CloseDevice(device);
					return;
				}
				if (!GetOverlappedResult(device.handle, &overlapped, &written, FALSE))
				{
					CloseDevice(device);
					return;
				}
			}

			device.outputDirty = false;
		}


		bool DualSensePadBackend::ParseInputReport(const Device& device, PadState& out) const
		{
			const uint8_t* report = device.latestReport;

			InputLayout layout;
			if (report[0] == REPORT_ID_EXTENDED)
			{
				// Bluetooth 拡張: レポート ID + シーケンスの 2 バイト分ずれる
				layout = { 2, 6, 9, 10 };
			}
			else if (report[0] == REPORT_ID_SIMPLE && device.connection == ConnectionType::Usb)
			{
				layout = { 1, 5, 8, 9 };
			}
			else if (report[0] == REPORT_ID_SIMPLE)
			{
				// Bluetooth 簡易: ボタンが前に詰まり、トリガーが後ろへ回る
				layout = { 1, 8, 5, 6 };
			}
			else
			{
				return false;
			}

			out.axes[static_cast<uint32_t>(PadAxis::LX)] = NormalizeStick(report[layout.stick + 0], false);
			out.axes[static_cast<uint32_t>(PadAxis::LY)] = NormalizeStick(report[layout.stick + 1], true);
			out.axes[static_cast<uint32_t>(PadAxis::RX)] = NormalizeStick(report[layout.stick + 2], false);
			out.axes[static_cast<uint32_t>(PadAxis::RY)] = NormalizeStick(report[layout.stick + 3], true);
			out.axes[static_cast<uint32_t>(PadAxis::LTrigger)] = report[layout.trigger + 0] / 255.0f;
			out.axes[static_cast<uint32_t>(PadAxis::RTrigger)] = report[layout.trigger + 1] / 255.0f;

			// □ × ○ △ は XInput の物理位置へ合わせる(× が下段 = A)。
			const uint8_t face = report[layout.faceDpad];
			out.buttons[static_cast<uint32_t>(PadButton::A)] = (face & FACE_CROSS)    != 0;
			out.buttons[static_cast<uint32_t>(PadButton::B)] = (face & FACE_CIRCLE)   != 0;
			out.buttons[static_cast<uint32_t>(PadButton::X)] = (face & FACE_SQUARE)   != 0;
			out.buttons[static_cast<uint32_t>(PadButton::Y)] = (face & FACE_TRIANGLE) != 0;

			const uint8_t hat  = face & 0x0F;
			const uint8_t dpad = (hat <= DPAD_NEUTRAL) ? HAT_TO_DPAD[hat] : 0;
			out.buttons[static_cast<uint32_t>(PadButton::DUp)]    = (dpad & DPAD_UP)    != 0;
			out.buttons[static_cast<uint32_t>(PadButton::DDown)]  = (dpad & DPAD_DOWN)  != 0;
			out.buttons[static_cast<uint32_t>(PadButton::DLeft)]  = (dpad & DPAD_LEFT)  != 0;
			out.buttons[static_cast<uint32_t>(PadButton::DRight)] = (dpad & DPAD_RIGHT) != 0;

			const uint8_t shoulder = report[layout.shoulder];
			out.buttons[static_cast<uint32_t>(PadButton::LB)]     = (shoulder & 0x01) != 0;   // L1
			out.buttons[static_cast<uint32_t>(PadButton::RB)]     = (shoulder & 0x02) != 0;   // R1
			out.buttons[static_cast<uint32_t>(PadButton::LT)]     = (shoulder & 0x04) != 0;   // L2
			out.buttons[static_cast<uint32_t>(PadButton::RT)]     = (shoulder & 0x08) != 0;   // R2
			out.buttons[static_cast<uint32_t>(PadButton::Back)]   = (shoulder & 0x10) != 0;   // Create
			out.buttons[static_cast<uint32_t>(PadButton::Start)]  = (shoulder & 0x20) != 0;   // Options
			out.buttons[static_cast<uint32_t>(PadButton::LStick)] = (shoulder & 0x40) != 0;   // L3
			out.buttons[static_cast<uint32_t>(PadButton::RStick)] = (shoulder & 0x80) != 0;   // R3
			return true;
		}


		void DualSensePadBackend::BuildEffectsState(const Device& device, uint8_t* dst) const
		{
			// レポート ID の次から始まる共通ペイロード(SDL: DS5EffectsState_t /
			// DS5W: createHidOutputBuffer)。先頭 2 バイトが「どの項目を書き換えるか」の有効フラグで、
			// 続いて右→左のランブル、10 バイト目から右トリガー、21 バイト目から左トリガーのエフェクト。
			// 有効フラグは DS5W が 0xFF / 0xF7 と全部立てるが、それだと音量や LED まで 0 で
			// 上書きしてしまうため、振動(bit0 互換ランブル + bit1 ハプティクス選択)と
			// トリガー(bit2 右 / bit3 左)に必要なビットだけを立てる。
			dst[0] = 0x0F;
			dst[1] = 0x02;
			dst[2] = device.rumbleRight;   // 高周波側(細かい振動)
			dst[3] = device.rumbleLeft;    // 低周波側(重い振動)

			WriteTriggerEffect(device.rightTrigger, &dst[10]);
			WriteTriggerEffect(device.leftTrigger,  &dst[21]);
		}


		void DualSensePadBackend::WriteTriggerEffect(const TriggerResistance& resistance, uint8_t* dst)
		{
			// トリガーエフェクトは 1 本あたり 11 バイト。先頭がモードで、
			// 0x01 = 開始位置から一定の抵抗(DS5W の ContinuousResitance)、0x00 = 抵抗なし。
			aq::memory::Clear(dst, TRIGGER_EFFECT_SIZE);
			if (resistance.strength == 0) { return; }   // 解除はモード 0x00 のまま送る

			dst[0] = 0x01;
			dst[1] = resistance.startPos;
			dst[2] = resistance.strength;
		}
	}
}
#endif // AQ_PLATFORM_WIN32
