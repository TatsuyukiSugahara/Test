#include "aq.h"
// macOS 以外、および ImGui 無効構成では空 TU にする
// (Win32 は imgui_impl_win32 が同じ責務を持つ)。
#if defined(AQ_PLATFORM_MAC) && defined(AQ_IMGUI)
#include "Platform/Mac/MacImGui.h"
#import <Cocoa/Cocoa.h>
#include <imgui/imgui.h>
#include <string>


namespace aq
{
	namespace platform
	{
		namespace MacImGui
		{
			namespace
			{
				/**
				 * macOS の仮想キーコード(Carbon の kVK_*)。
				 *
				 * `Carbon.framework` を引くためだけにリンクを増やしたくないので、値をここに写す
				 * (方針は aq::hid::CocoaInputSink と同じ)。ANSI 配列を基準とした**物理位置**の
				 * 番号で、JIS 配列でも同じ物理キーに同じ番号が振られる。
				 */
				enum : uint16_t
				{
					kVK_ANSI_A              = 0x00,
					kVK_ANSI_S              = 0x01,
					kVK_ANSI_D              = 0x02,
					kVK_ANSI_F              = 0x03,
					kVK_ANSI_H              = 0x04,
					kVK_ANSI_G              = 0x05,
					kVK_ANSI_Z              = 0x06,
					kVK_ANSI_X              = 0x07,
					kVK_ANSI_C              = 0x08,
					kVK_ANSI_V              = 0x09,
					kVK_ANSI_B              = 0x0B,
					kVK_ANSI_Q              = 0x0C,
					kVK_ANSI_W              = 0x0D,
					kVK_ANSI_E              = 0x0E,
					kVK_ANSI_R              = 0x0F,
					kVK_ANSI_Y              = 0x10,
					kVK_ANSI_T              = 0x11,
					kVK_ANSI_1              = 0x12,
					kVK_ANSI_2              = 0x13,
					kVK_ANSI_3              = 0x14,
					kVK_ANSI_4              = 0x15,
					kVK_ANSI_6              = 0x16,
					kVK_ANSI_5              = 0x17,
					kVK_ANSI_Equal          = 0x18,
					kVK_ANSI_9              = 0x19,
					kVK_ANSI_7              = 0x1A,
					kVK_ANSI_Minus          = 0x1B,
					kVK_ANSI_8              = 0x1C,
					kVK_ANSI_0              = 0x1D,
					kVK_ANSI_RightBracket   = 0x1E,
					kVK_ANSI_O              = 0x1F,
					kVK_ANSI_U              = 0x20,
					kVK_ANSI_LeftBracket    = 0x21,
					kVK_ANSI_I              = 0x22,
					kVK_ANSI_P              = 0x23,
					kVK_Return              = 0x24,
					kVK_ANSI_L              = 0x25,
					kVK_ANSI_J              = 0x26,
					kVK_ANSI_Quote          = 0x27,
					kVK_ANSI_K              = 0x28,
					kVK_ANSI_Semicolon      = 0x29,
					kVK_ANSI_Backslash      = 0x2A,
					kVK_ANSI_Comma          = 0x2B,
					kVK_ANSI_Slash          = 0x2C,
					kVK_ANSI_N              = 0x2D,
					kVK_ANSI_M              = 0x2E,
					kVK_ANSI_Period         = 0x2F,
					kVK_Tab                 = 0x30,
					kVK_Space               = 0x31,
					kVK_ANSI_Grave          = 0x32,
					kVK_Delete              = 0x33,   // Backspace(Mac の delete キー)
					kVK_Escape              = 0x35,
					kVK_RightCommand        = 0x36,
					kVK_Command             = 0x37,
					kVK_Shift               = 0x38,
					kVK_CapsLock            = 0x39,
					kVK_Option              = 0x3A,
					kVK_Control             = 0x3B,
					kVK_RightShift          = 0x3C,
					kVK_RightOption         = 0x3D,
					kVK_RightControl        = 0x3E,
					kVK_ANSI_KeypadDecimal  = 0x41,
					kVK_ANSI_KeypadMultiply = 0x43,
					kVK_ANSI_KeypadPlus     = 0x45,
					kVK_ANSI_KeypadDivide   = 0x4B,
					kVK_ANSI_KeypadEnter    = 0x4C,
					kVK_ANSI_KeypadMinus    = 0x4E,
					kVK_ANSI_KeypadEquals   = 0x51,
					kVK_ANSI_Keypad0        = 0x52,
					kVK_ANSI_Keypad1        = 0x53,
					kVK_ANSI_Keypad2        = 0x54,
					kVK_ANSI_Keypad3        = 0x55,
					kVK_ANSI_Keypad4        = 0x56,
					kVK_ANSI_Keypad5        = 0x57,
					kVK_ANSI_Keypad6        = 0x58,
					kVK_ANSI_Keypad7        = 0x59,
					kVK_ANSI_Keypad8        = 0x5B,
					kVK_ANSI_Keypad9        = 0x5C,
					kVK_F5                  = 0x60,
					kVK_F6                  = 0x61,
					kVK_F7                  = 0x62,
					kVK_F3                  = 0x63,
					kVK_F8                  = 0x64,
					kVK_F9                  = 0x65,
					kVK_F11                 = 0x67,
					kVK_F10                 = 0x6D,
					kVK_F12                 = 0x6F,
					kVK_Help                = 0x72,   // Insert 相当
					kVK_Home                = 0x73,
					kVK_PageUp              = 0x74,
					kVK_ForwardDelete       = 0x75,
					kVK_F4                  = 0x76,
					kVK_End                 = 0x77,
					kVK_F2                  = 0x78,
					kVK_PageDown            = 0x79,
					kVK_F1                  = 0x7A,
					kVK_LeftArrow           = 0x7B,
					kVK_RightArrow          = 0x7C,
					kVK_DownArrow           = 0x7D,
					kVK_UpArrow             = 0x7E,
				};


				/** macOS 仮想キーコードと ImGuiKey の対応 */
				struct KeyMapEntry
				{
					uint16_t macKeyCode;
					ImGuiKey key;
				};


				// aq::hid::CocoaInputSink の KEY_MAP とは共有しない。あちらは KeyBoardType
				// (ゲームが見る 16 キー)への写像で、写像先の enum が違う(設計書 P4b)。
				// 表に無いキーは ImGuiKey_None として無視する。
				static constexpr KeyMapEntry KEY_MAP[] =
				{
					{ kVK_ANSI_A,              ImGuiKey_A              },
					{ kVK_ANSI_B,              ImGuiKey_B              },
					{ kVK_ANSI_C,              ImGuiKey_C              },
					{ kVK_ANSI_D,              ImGuiKey_D              },
					{ kVK_ANSI_E,              ImGuiKey_E              },
					{ kVK_ANSI_F,              ImGuiKey_F              },
					{ kVK_ANSI_G,              ImGuiKey_G              },
					{ kVK_ANSI_H,              ImGuiKey_H              },
					{ kVK_ANSI_I,              ImGuiKey_I              },
					{ kVK_ANSI_J,              ImGuiKey_J              },
					{ kVK_ANSI_K,              ImGuiKey_K              },
					{ kVK_ANSI_L,              ImGuiKey_L              },
					{ kVK_ANSI_M,              ImGuiKey_M              },
					{ kVK_ANSI_N,              ImGuiKey_N              },
					{ kVK_ANSI_O,              ImGuiKey_O              },
					{ kVK_ANSI_P,              ImGuiKey_P              },
					{ kVK_ANSI_Q,              ImGuiKey_Q              },
					{ kVK_ANSI_R,              ImGuiKey_R              },
					{ kVK_ANSI_S,              ImGuiKey_S              },
					{ kVK_ANSI_T,              ImGuiKey_T              },
					{ kVK_ANSI_U,              ImGuiKey_U              },
					{ kVK_ANSI_V,              ImGuiKey_V              },
					{ kVK_ANSI_W,              ImGuiKey_W              },
					{ kVK_ANSI_X,              ImGuiKey_X              },
					{ kVK_ANSI_Y,              ImGuiKey_Y              },
					{ kVK_ANSI_Z,              ImGuiKey_Z              },

					{ kVK_ANSI_0,              ImGuiKey_0              },
					{ kVK_ANSI_1,              ImGuiKey_1              },
					{ kVK_ANSI_2,              ImGuiKey_2              },
					{ kVK_ANSI_3,              ImGuiKey_3              },
					{ kVK_ANSI_4,              ImGuiKey_4              },
					{ kVK_ANSI_5,              ImGuiKey_5              },
					{ kVK_ANSI_6,              ImGuiKey_6              },
					{ kVK_ANSI_7,              ImGuiKey_7              },
					{ kVK_ANSI_8,              ImGuiKey_8              },
					{ kVK_ANSI_9,              ImGuiKey_9              },

					{ kVK_ANSI_Quote,          ImGuiKey_Apostrophe     },
					{ kVK_ANSI_Comma,          ImGuiKey_Comma          },
					{ kVK_ANSI_Minus,          ImGuiKey_Minus          },
					{ kVK_ANSI_Period,         ImGuiKey_Period         },
					{ kVK_ANSI_Slash,          ImGuiKey_Slash          },
					{ kVK_ANSI_Semicolon,      ImGuiKey_Semicolon      },
					{ kVK_ANSI_Equal,          ImGuiKey_Equal          },
					{ kVK_ANSI_LeftBracket,    ImGuiKey_LeftBracket    },
					{ kVK_ANSI_Backslash,      ImGuiKey_Backslash      },
					{ kVK_ANSI_RightBracket,   ImGuiKey_RightBracket   },
					{ kVK_ANSI_Grave,          ImGuiKey_GraveAccent    },

					{ kVK_Return,              ImGuiKey_Enter          },
					{ kVK_Tab,                 ImGuiKey_Tab            },
					{ kVK_Space,               ImGuiKey_Space          },
					{ kVK_Delete,              ImGuiKey_Backspace      },
					{ kVK_Escape,              ImGuiKey_Escape         },

					{ kVK_LeftArrow,           ImGuiKey_LeftArrow      },
					{ kVK_RightArrow,          ImGuiKey_RightArrow     },
					{ kVK_UpArrow,             ImGuiKey_UpArrow        },
					{ kVK_DownArrow,           ImGuiKey_DownArrow      },

					{ kVK_Help,                ImGuiKey_Insert         },
					{ kVK_Home,                ImGuiKey_Home           },
					{ kVK_End,                 ImGuiKey_End            },
					{ kVK_PageUp,              ImGuiKey_PageUp         },
					{ kVK_PageDown,            ImGuiKey_PageDown       },
					{ kVK_ForwardDelete,       ImGuiKey_Delete         },

					{ kVK_F1,                  ImGuiKey_F1             },
					{ kVK_F2,                  ImGuiKey_F2             },
					{ kVK_F3,                  ImGuiKey_F3             },
					{ kVK_F4,                  ImGuiKey_F4             },
					{ kVK_F5,                  ImGuiKey_F5             },
					{ kVK_F6,                  ImGuiKey_F6             },
					{ kVK_F7,                  ImGuiKey_F7             },
					{ kVK_F8,                  ImGuiKey_F8             },
					{ kVK_F9,                  ImGuiKey_F9             },
					{ kVK_F10,                 ImGuiKey_F10            },
					{ kVK_F11,                 ImGuiKey_F11            },
					{ kVK_F12,                 ImGuiKey_F12            },

					{ kVK_ANSI_Keypad0,        ImGuiKey_Keypad0        },
					{ kVK_ANSI_Keypad1,        ImGuiKey_Keypad1        },
					{ kVK_ANSI_Keypad2,        ImGuiKey_Keypad2        },
					{ kVK_ANSI_Keypad3,        ImGuiKey_Keypad3        },
					{ kVK_ANSI_Keypad4,        ImGuiKey_Keypad4        },
					{ kVK_ANSI_Keypad5,        ImGuiKey_Keypad5        },
					{ kVK_ANSI_Keypad6,        ImGuiKey_Keypad6        },
					{ kVK_ANSI_Keypad7,        ImGuiKey_Keypad7        },
					{ kVK_ANSI_Keypad8,        ImGuiKey_Keypad8        },
					{ kVK_ANSI_Keypad9,        ImGuiKey_Keypad9        },
					{ kVK_ANSI_KeypadDecimal,  ImGuiKey_KeypadDecimal  },
					{ kVK_ANSI_KeypadDivide,   ImGuiKey_KeypadDivide   },
					{ kVK_ANSI_KeypadMultiply, ImGuiKey_KeypadMultiply },
					{ kVK_ANSI_KeypadMinus,    ImGuiKey_KeypadSubtract },
					{ kVK_ANSI_KeypadPlus,     ImGuiKey_KeypadAdd      },
					{ kVK_ANSI_KeypadEnter,    ImGuiKey_KeypadEnter    },
					{ kVK_ANSI_KeypadEquals,   ImGuiKey_KeypadEqual    },

					// 修飾キーは keyDown/keyUp ではなく flagsChanged で来る。
					{ kVK_Control,             ImGuiKey_LeftCtrl       },
					{ kVK_Shift,               ImGuiKey_LeftShift      },
					{ kVK_Option,              ImGuiKey_LeftAlt        },
					{ kVK_Command,             ImGuiKey_LeftSuper      },
					{ kVK_RightControl,        ImGuiKey_RightCtrl      },
					{ kVK_RightShift,          ImGuiKey_RightShift     },
					{ kVK_RightOption,         ImGuiKey_RightAlt       },
					{ kVK_RightCommand,        ImGuiKey_RightSuper     },
					{ kVK_CapsLock,            ImGuiKey_CapsLock       },
				};


				/** ImGuiIO::MouseDown の要素数。これを外れたボタン番号は捨てる */
				static constexpr int32_t MOUSE_BUTTON_COUNT = 5;

				/** トラックパッドのピクセル量を行単位の尺度へ落とす係数 */
				static constexpr float PRECISE_WHEEL_SCALE = 0.1f;


				/** クリップボード読み出しの保持先(imgui は返したポインタを後から参照する) */
				std::string g_clipboardText;

				/** 押下中として ImGui へ入れたキー(Command 離しの一掃用) */
				bool g_keyDown[ImGuiKey_NamedKey_COUNT] = {};

				/** Command の押下状態 */
				bool g_commandHeld = false;

				/** 直前に適用したカーソル形状(毎フレーム NSCursor へ set しないため) */
				ImGuiMouseCursor g_currentCursor = ImGuiMouseCursor_Arrow;


				/** 仮想キーコードから ImGuiKey を引く。対応が無ければ ImGuiKey_None */
				ImGuiKey ToImGuiKey(const uint16_t macKeyCode)
				{
					for (const KeyMapEntry& entry : KEY_MAP)
					{
						if (entry.macKeyCode == macKeyCode)
						{
							return entry.key;
						}
					}
					return ImGuiKey_None;
				}


				/** 押下状態の記録。ImGuiKey は 512 始まりなので NamedKey_BEGIN を引いて索く */
				void RecordKeyDown(const ImGuiKey key, const bool pressed)
				{
					const int index = static_cast<int>(key) - ImGuiKey_NamedKey_BEGIN;
					if (index < 0 || index >= ImGuiKey_NamedKey_COUNT)
					{
						return;
					}
					g_keyDown[index] = pressed;
				}


				/** 押下中として記録しているキーをすべて離す */
				void ReleaseAllKeys()
				{
					ImGuiIO& io = ImGui::GetIO();
					for (int i = 0; i < ImGuiKey_NamedKey_COUNT; ++i)
					{
						if (!g_keyDown[i])
						{
							continue;
						}
						g_keyDown[i] = false;
						io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_NamedKey_BEGIN + i), false);
					}
				}


				/** 押下状態の記録だけを消す(ImGui へはイベントを送らない) */
				void ClearKeyRecords()
				{
					for (bool& down : g_keyDown)
					{
						down = false;
					}
				}


				/**
				 * flagsChanged の keyCode に対応する修飾ビット。
				 *
				 * 左右は同じビットを共有するため、両方を同時に押して片方だけ離した場合は
				 * 個別キー(ImGuiKey_LeftShift 等)が押しっぱなしに見える。ショートカット判定で
				 * 使う ImGuiMod_* 側は modifierFlags をそのまま入れ直すので影響しない。
				 */
				NSEventModifierFlags ModifierMaskOf(const uint16_t macKeyCode)
				{
					switch (macKeyCode)
					{
					case kVK_Control:
					case kVK_RightControl:
						return NSEventModifierFlagControl;

					case kVK_Shift:
					case kVK_RightShift:
						return NSEventModifierFlagShift;

					case kVK_Option:
					case kVK_RightOption:
						return NSEventModifierFlagOption;

					case kVK_Command:
					case kVK_RightCommand:
						return NSEventModifierFlagCommand;

					case kVK_CapsLock:
						return NSEventModifierFlagCapsLock;

					default:
						return 0;
					}
				}


				/** ImGui のカーソル形状に対応する NSCursor。対応が無ければ矢印 */
				NSCursor* ToNSCursor(const ImGuiMouseCursor cursor)
				{
					switch (cursor)
					{
					case ImGuiMouseCursor_TextInput:  return [NSCursor IBeamCursor];
					case ImGuiMouseCursor_ResizeAll:  return [NSCursor closedHandCursor];
					case ImGuiMouseCursor_ResizeNS:   return [NSCursor resizeUpDownCursor];
					case ImGuiMouseCursor_ResizeEW:   return [NSCursor resizeLeftRightCursor];
					case ImGuiMouseCursor_ResizeNESW: return [NSCursor closedHandCursor];
					case ImGuiMouseCursor_ResizeNWSE: return [NSCursor closedHandCursor];
					case ImGuiMouseCursor_Hand:       return [NSCursor pointingHandCursor];
					case ImGuiMouseCursor_NotAllowed: return [NSCursor operationNotAllowedCursor];
					default:                          return [NSCursor arrowCursor];
					}
				}


				/** ImGui が要求する形状へ NSCursor を合わせる */
				void UpdateMouseCursor()
				{
					ImGuiIO& io = ImGui::GetIO();
					if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0)
					{
						return;
					}

					// 形状が変わったときだけ触る。hide / unhide は対で呼ぶ必要があるため、
					// この「変化時のみ」が対の釣り合いも保証している。
					const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
					if (cursor == g_currentCursor)
					{
						return;
					}
					g_currentCursor = cursor;

					if (cursor == ImGuiMouseCursor_None)
					{
						[NSCursor hide];
						return;
					}
					[NSCursor unhide];
					[ToNSCursor(cursor) set];
				}


				/** クリップボードの読み出し(ImGuiPlatformIO から呼ばれる) */
				const char* GetClipboardText(ImGuiContext* context)
				{
					(void)context;

					@autoreleasepool
					{
						NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
						NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
						const char* utf8 = (text != nil) ? [text UTF8String] : nullptr;
						// autorelease される NSString をそのまま返せないので、こちらで抱える。
						g_clipboardText = (utf8 != nullptr) ? utf8 : "";
					}
					return g_clipboardText.c_str();
				}


				/** クリップボードへの書き込み(ImGuiPlatformIO から呼ばれる) */
				void SetClipboardText(ImGuiContext* context, const char* text)
				{
					(void)context;
					if (text == nullptr)
					{
						return;
					}

					@autoreleasepool
					{
						NSString* value = [NSString stringWithUTF8String:text];
						if (value == nil)
						{
							return;
						}
						NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
						[pasteboard declareTypes:@[ NSPasteboardTypeString ] owner:nil];
						[pasteboard setString:value forType:NSPasteboardTypeString];
					}
				}


				// イベントの位置をビュー座標(左上原点)へ直して ImGui へ渡す。
				// Cocoa は左下原点なので Y を反転する。§8-13 の決定で contentsScale = 1 に
				// してあるため、ポイント座標と描画ピクセルは 1:1 で倍率補正は要らない
				// (PlatformMac.mm の PushMousePosition と同じ変換)。
				void PushMousePosition(NSEvent* event, NSView* view)
				{
					if (view == nil)
					{
						return;
					}
					const NSPoint inView = [view convertPoint:[event locationInWindow] fromView:nil];
					const CGFloat height = [view bounds].size.height;
					ImGui::GetIO().AddMousePosEvent(
						static_cast<float>(inView.x),
						static_cast<float>(height - inView.y));
				}


				/** keyDown の characters を文字入力として流す */
				void PushInputCharacters(NSString* characters)
				{
					if (characters == nil)
					{
						return;
					}
					const char* utf8 = [characters UTF8String];
					if (utf8 == nullptr)
					{
						return;
					}

					// characters には改行・Backspace・Escape などの制御文字も混ざる。
					// これらはキーイベント側で扱うので、文字入力としては落とす
					// (多バイト文字の後続バイトは 0x80 以上なので巻き込まない)。
					std::string filtered;
					for (const char* cursor = utf8; *cursor != '\0'; ++cursor)
					{
						const unsigned char code = static_cast<unsigned char>(*cursor);
						if (code < 0x20 || code == 0x7F)
						{
							continue;
						}
						filtered.push_back(*cursor);
					}

					if (!filtered.empty())
					{
						ImGui::GetIO().AddInputCharactersUTF8(filtered.c_str());
					}
				}
			}


			bool Init()
			{
				EngineAssertMsg(ImGui::GetCurrentContext() != nullptr, "ImGui のコンテキストが未生成です");
				if (ImGui::GetCurrentContext() == nullptr)
				{
					return false;
				}

				ImGuiIO& io = ImGui::GetIO();
				io.BackendPlatformName = "aq_mac";
				// カーソル形状は NewFrame で反映する。SetMousePos は実装しないので
				// HasSetMousePos は立てない。
				io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

				// クリップボードの口は 1.91.1 で ImGuiIO から ImGuiPlatformIO へ移った
				// (同梱の imgui は 1.92.0 WIP。io 側は旧 API の互換として残っているだけ)。
				ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
				platformIo.Platform_GetClipboardTextFn = GetClipboardText;
				platformIo.Platform_SetClipboardTextFn = SetClipboardText;
				platformIo.Platform_ClipboardUserData  = nullptr;

				g_clipboardText.clear();
				g_commandHeld   = false;
				g_currentCursor = ImGuiMouseCursor_Arrow;
				ClearKeyRecords();
				return true;
			}


			void Shutdown()
			{
				if (ImGui::GetCurrentContext() == nullptr)
				{
					return;
				}

				ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
				platformIo.Platform_GetClipboardTextFn = nullptr;
				platformIo.Platform_SetClipboardTextFn = nullptr;
				platformIo.Platform_ClipboardUserData  = nullptr;

				ImGuiIO& io = ImGui::GetIO();
				io.BackendPlatformName = nullptr;
				io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;

				// カーソルを隠したままプロセスを進めない(hide の対の unhide)。
				if (g_currentCursor == ImGuiMouseCursor_None)
				{
					[NSCursor unhide];
				}
				g_currentCursor = ImGuiMouseCursor_Arrow;
				g_commandHeld   = false;
				ClearKeyRecords();
				g_clipboardText.clear();
				g_clipboardText.shrink_to_fit();
			}


			void NewFrame()
			{
				ImGuiIO& io = ImGui::GetIO();

				// プラットフォームバックエンドが埋める 2 つ(Application.cpp の Mac 分岐から移設)。
				//  - DisplaySize: 0 のままだと ImGui::NewFrame のサニティチェックで停止する
				//  - DeltaTime  : 0 以下だと同じくアサートに掛かる(初回フレームは実測値が無い)
				// DisplayFramebufferScale は既定の (1,1) のまま(設計書 §8-13 で contentsScale = 1
				// に固定してあり、ポイント座標と描画ピクセルが 1:1 のため)。
				io.DisplaySize = ImVec2(static_cast<float>(Engine::Get().GetScreenWidth()),
				                        static_cast<float>(Engine::Get().GetScreenHeight()));
				const float deltaTime = Engine::GetDeltaTime();
				io.DeltaTime = (deltaTime > 0.0f) ? deltaTime : (1.0f / 60.0f);

				UpdateMouseCursor();
			}


			void OnFocusChanged(const bool focused)
			{
				if (ImGui::GetCurrentContext() == nullptr)
				{
					return;
				}

				// 失うときは先に押下を解いておく。ImGui 側も AppFocusLost で状態を消すが、
				// こちらの g_keyDown を残すと復帰後の一掃対象がずれるため揃える。
				if (!focused)
				{
					ReleaseAllKeys();
				}
				g_commandHeld = false;

				ImGui::GetIO().AddFocusEvent(focused);
			}


			void HandleEvent(NSEvent* event, NSView* view)
			{
				// 初期化前(コンテキスト生成前)に PumpEvents が回ることがある。
				if (event == nil || ImGui::GetCurrentContext() == nullptr)
				{
					return;
				}

				ImGuiIO& io = ImGui::GetIO();

				switch ([event type])
				{
				case NSEventTypeMouseMoved:
				case NSEventTypeLeftMouseDragged:
				case NSEventTypeRightMouseDragged:
				case NSEventTypeOtherMouseDragged:
					PushMousePosition(event, view);
					break;

				case NSEventTypeLeftMouseDown:
				case NSEventTypeLeftMouseUp:
				case NSEventTypeRightMouseDown:
				case NSEventTypeRightMouseUp:
				case NSEventTypeOtherMouseDown:
				case NSEventTypeOtherMouseUp:
				{
					const NSEventType type = [event type];
					const bool pressed = (type == NSEventTypeLeftMouseDown)
					                  || (type == NSEventTypeRightMouseDown)
					                  || (type == NSEventTypeOtherMouseDown);
					// buttonNumber は左=0 / 右=1 / 中=2。ImGui が持つのは 5 個まで。
					const int32_t button = static_cast<int32_t>([event buttonNumber]);
					if (button < 0 || button >= MOUSE_BUTTON_COUNT)
					{
						break;
					}
					io.AddMouseButtonEvent(button, pressed);
					// クリックだけでカーソルが動かない場合もあるので位置も更新しておく。
					if (pressed)
					{
						PushMousePosition(event, view);
					}
					break;
				}

				case NSEventTypeScrollWheel:
				{
					float wheelX = static_cast<float>([event scrollingDeltaX]);
					float wheelY = static_cast<float>([event scrollingDeltaY]);
					// トラックパッド(精密デルタ)はピクセル量で来るので行単位の尺度へ落とす。
					if ([event hasPreciseScrollingDeltas])
					{
						wheelX *= PRECISE_WHEEL_SCALE;
						wheelY *= PRECISE_WHEEL_SCALE;
					}
					if (wheelX == 0.0f && wheelY == 0.0f)
					{
						break;
					}
					// Cocoa は右方向が負、ImGui は右方向が正。
					io.AddMouseWheelEvent(-wheelX, wheelY);
					break;
				}

				case NSEventTypeKeyDown:
				case NSEventTypeKeyUp:
				{
					const bool pressed = ([event type] == NSEventTypeKeyDown);
					const ImGuiKey key = ToImGuiKey(static_cast<uint16_t>([event keyCode]));
					if (key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, pressed);
						RecordKeyDown(key, pressed);
					}

					// Command 併用の keyDown はメニューショートカット。文字としては入れない。
					if (pressed && ([event modifierFlags] & NSEventModifierFlagCommand) == 0)
					{
						PushInputCharacters([event characters]);
					}
					break;
				}

				case NSEventTypeFlagsChanged:
				{
					const NSEventModifierFlags flags = [event modifierFlags];
					const bool commandPressed = (flags & NSEventModifierFlagCommand) != 0;

					io.AddKeyEvent(ImGuiMod_Ctrl,  (flags & NSEventModifierFlagControl) != 0);
					io.AddKeyEvent(ImGuiMod_Shift, (flags & NSEventModifierFlagShift) != 0);
					io.AddKeyEvent(ImGuiMod_Alt,   (flags & NSEventModifierFlagOption) != 0);
					io.AddKeyEvent(ImGuiMod_Super, commandPressed);

					// 変化した修飾キー単体も入れておく(ImGuiKey_LeftShift 等)。
					const uint16_t macKeyCode = static_cast<uint16_t>([event keyCode]);
					const NSEventModifierFlags mask = ModifierMaskOf(macKeyCode);
					const ImGuiKey key = ToImGuiKey(macKeyCode);
					if (mask != 0 && key != ImGuiKey_None)
					{
						const bool pressed = (flags & mask) != 0;
						io.AddKeyEvent(key, pressed);
						RecordKeyDown(key, pressed);
					}

					// Command を押している間、macOS は通常キーの keyUp を配送しない。
					// 押しっぱなしのまま残るのを防ぐため、離された時点で一掃する
					// (aq::hid::CocoaInputSink::OnModifierFlagsChanged と同じ対処)。
					if (g_commandHeld && !commandPressed)
					{
						ReleaseAllKeys();
					}
					g_commandHeld = commandPressed;
					break;
				}

				default:
					break;
				}
			}
		}
	}
}
#endif // AQ_PLATFORM_MAC && AQ_IMGUI
