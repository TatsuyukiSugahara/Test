#include "aq.h"
// Win32 専用実装。UWP(Xbox)構成ではデスクトップ API(CreateWindow 等)が使えないため
// 本体をガードして空 TU にする(UWP は PlatformUWP.cpp、Mac は PlatformMac.mm が代替)。
#if defined(AQ_PLATFORM_WIN32)
#include "Platform/PlatformWin32.h"
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
// header 内で #if 0 されているため、使用する .cpp で前方宣言が必要
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif


// 起動診断ログ(StartupLog)の既定実装は aq.cpp にある(UWP 以外で共通)。


namespace aq
{
	namespace platform
	{
		namespace
		{
			// ユーザーデータを置くフォルダ名。ゲーム名が変わったらここを変える。
			static constexpr char APP_FOLDER_NAME[] = "AquaDash";
		}


		PlatformWin32::PlatformWin32(HINSTANCE hInstance, int nCmdShow)
			: hInstance_(hInstance)
			, nCmdShow_(nCmdShow)
			, hWnd_(nullptr)
			, userDataDirectory_()
			, userDataDirectoryResolved_(false)
		{
		}


		PlatformWin32::~PlatformWin32()
		{
		}


		bool PlatformWin32::CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out)
		{
			EngineAssert(desc.width);
			EngineAssert(desc.height);

			// 背景ブラシは黒にする。初回 Present までの初期化中(約 1〜2 秒)に OS 既定の白が
			// 見えていたため、その間も黒画面にしておく(描画開始後は D3D が全面を塗るので影響なし)。
			WNDCLASSEX wc = {
				sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L,
				GetModuleHandle(nullptr), nullptr, nullptr,
				static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)), nullptr,
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


		const char* PlatformWin32::GetUserDataDirectory()
		{
			if (!userDataDirectoryResolved_)
			{
				userDataDirectoryResolved_ = true;

				// %LOCALAPPDATA%\AquaDash\ を使う。実行ディレクトリは Program Files 配下だと
				// 書き込めないため、ユーザーごとのローカルアプリデータへ置く。
				// ユーザーホームは他アプリと共有なので、アプリ名の階層をここで足す。
				char localAppData[MAX_PATH] = {};
				const DWORD len = ::GetEnvironmentVariableA(
					"LOCALAPPDATA", localAppData, static_cast<DWORD>(_countof(localAppData)));
				if (len > 0 && len < _countof(localAppData))
				{
					std::string directory = localAppData;
					directory += "\\";
					directory += APP_FOLDER_NAME;

					// 既にあれば ERROR_ALREADY_EXISTS で失敗するので、それは成功として扱う。
					if (::CreateDirectoryA(directory.c_str(), nullptr)
					 || ::GetLastError() == ERROR_ALREADY_EXISTS)
					{
						directory += "\\";
						userDataDirectory_ = directory;
					}
				}
			}

			return userDataDirectory_.empty() ? nullptr : userDataDirectory_.c_str();
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
#endif // AQ_PLATFORM_WIN32
