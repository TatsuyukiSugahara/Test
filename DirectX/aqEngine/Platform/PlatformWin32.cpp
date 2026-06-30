#include "aq.h"
#include "Platform/PlatformWin32.h"
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
// header 内で #if 0 されているため、使用する .cpp で前方宣言が必要
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif


namespace aq
{
	namespace platform
	{
		PlatformWin32::PlatformWin32(HINSTANCE hInstance, int nCmdShow)
			: hInstance_(hInstance)
			, nCmdShow_(nCmdShow)
			, hWnd_(nullptr)
		{
		}


		PlatformWin32::~PlatformWin32()
		{
		}


		bool PlatformWin32::CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out)
		{
			EngineAssert(desc.width);
			EngineAssert(desc.height);

			WNDCLASSEX wc = {
				sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L,
				GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
				TEXT("Application"), nullptr
			};
			RegisterClassEx(&wc);

			RECT rc = { 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
			AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
			hWnd_ = CreateWindow(
				TEXT("Application"), TEXT("Application"),
				WS_OVERLAPPEDWINDOW, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
				nullptr, nullptr, hInstance_, nullptr
			);

			ShowWindow(hWnd_, nCmdShow_);

			out.handle = hWnd_;
			return hWnd_ != nullptr;
		}


		bool PlatformWin32::PumpEvents()
		{
			MSG msg = { 0 };
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					return false;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			return true;
		}


		const char* PlatformWin32::GetContentRoot()
		{
			// Win32 は従来どおり Resource 側の探索（ソースツリー基点）に委ねる。
			// UWP ではパッケージ install フォルダを返す実装に差し替える。
			return nullptr;
		}


		LRESULT CALLBACK PlatformWin32::MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
#ifdef AQ_IMGUI
			if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
			{
				return true;
			}
#endif

			switch (msg)
			{
				case WM_PAINT:
				{
					PAINTSTRUCT ps;
					HDC hdc = BeginPaint(hWnd, &ps);
					UNREFERENCED_PARAMETER(hdc);
					EndPaint(hWnd, &ps);
					break;
				}
				case WM_DESTROY:
				{
					PostQuitMessage(0);
					break;
				}
				default:
				{
					return DefWindowProc(hWnd, msg, wParam, lParam);
				}
			}
			return 0;
		}
	}
}
