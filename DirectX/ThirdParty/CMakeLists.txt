# ============================================================================
#  ThirdParty ライブラリ
#
#  既存 vcxproj では DirectXTex / ufbx / spirv_reflect は Engine.vcxproj に
#  同梱コンパイル、imgui は imgui.vcxproj(静的ライブラリ)という構成だが、
#  CMake では全て独立した静的ライブラリターゲットにまとめる。
#  最終的に Game.exe へリンクされるシンボル集合は同じ。
#
#  include の書き方が <imgui/imgui.h> / <DirectXTex/DirectXTex.h> /
#  <vma/vk_mem_alloc.h> / <spirv_reflect/spirv_reflect.h> / "ufbx/ufbx.h"
#  なので、公開インクルードディレクトリは全て「ThirdParty ディレクトリ自身」。
# ============================================================================

set(AQ_THIRDPARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# vcxproj 側と同じ。CRT のセキュア版でない関数(fopen / strncpy 等)の警告を止める。
if(MSVC)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()


# ----------------------------------------------------------------------------
#  DirectXMath(ヘッダオンリ・同梱)
#
#  microsoft/DirectXMath の jun2026(3.21b / DIRECTX_MATH_VERSION 321)の Inc/ のみ。
#  macOS には Windows SDK が無く <DirectXMath.h> の入手元が無いため同梱する。
#  版ずれで挙動差が出るのを避けるため、Windows も SDK 版ではなく同梱側に一本化する
#  (設計書 §0「数学」/ §6)。
#
#  公開ディレクトリは 2 つ。
#   - ThirdParty 自身      … aq.h が書く <DirectXMath/Inc/DirectXMath.h> 用
#                            (他の同梱ライブラリと同じ書き方)
#   - DirectXMath/Inc 自身 … DirectXTex(<DirectXMath.h> / <DirectXPackedVector.h>)と
#                            aqEngine/Math/Vector.h が書く「無修飾」の include 用。
#                            こちらを通さないと Windows では SDK 版が拾われ、同梱版と
#                            2 種類の DirectXMath.h が同一 TU に入って多重定義になる。
# ----------------------------------------------------------------------------
add_library(directxmath INTERFACE)
target_include_directories(directxmath INTERFACE
	"${AQ_THIRDPARTY_ROOT}"
	"${AQ_THIRDPARTY_ROOT}/DirectXMath/Inc"
)


# ----------------------------------------------------------------------------
#  DirectX-Headers(ヘッダオンリ・同梱。非 Windows でのみインクルードパスに載せる)
#
#  microsoft/DirectX-Headers v1.619.5 から、非 Windows 経路に要るものだけを抜粋。
#  DirectXTex の非 Windows 分岐が要求するのは以下で、これで閉じている。
#   - DirectXTex.h   : <directx/dxgiformat.h> / <wsl/winadapter.h>
#   - DirectXTexP.h  : <wsl/winadapter.h> / <wsl/wrladapter.h> / <directx/d3d12.h>
#  d3d12.h からの推移的な依存として d3d12sdklayers.h / d3dcommon.h / dxgicommon.h と
#  wsl/stubs/(rpc.h・rpcndr.h・basetsd.h・unknwn(base).h・oaidl.h・ocidl.h・
#  winapifamily.h・wrl/)が要る。d3dx12*・dxcore・DirectML・video 系は誰も見ないため入れない。
#  SAL 注釈(_In_ 等)と COM_NO_WINDOWS_H は wsl/stubs/basetsd.h が定義する
#  (このバージョンに sal.h という名前のファイルは無い)。
#
#  Windows では載せない。stubs/ の basetsd.h・unknwn.h・ocidl.h 等が Windows SDK の
#  同名ヘッダを隠してしまうため、公開すると Windows 側の挙動を壊す。
# ----------------------------------------------------------------------------
add_library(directx-headers INTERFACE)

if(NOT WIN32)
	target_include_directories(directx-headers INTERFACE
		"${AQ_THIRDPARTY_ROOT}/DirectX-Headers/include"
		"${AQ_THIRDPARTY_ROOT}/DirectX-Headers/include/wsl/stubs"
	)
endif()


# ----------------------------------------------------------------------------
#  DirectXTex
#
#  Windows は同梱されている cpp を全てビルドする(Engine.vcxproj と同一集合)。
#  非 Windows では WIC(PNG/JPG)が使えないため DirectXTexWIC.cpp を除外し、
#  DDS/TGA/HDR + BC ソフトコーデック経路だけを使う(設計書 §6)。
#  ※ 同梱ソースには BCDirectCompute.cpp / DirectXTexD3D11.cpp /
#     DirectXTexD3D12.cpp が最初から含まれていないため、追加の除外は不要。
#
#  非 Windows でのビルドには SAL 互換ヘッダ(ThirdParty/DirectX-Headers/
#  include/wsl)と同梱 DirectXMath が要る。どちらも上で INTERFACE ターゲットにしてある。
# ----------------------------------------------------------------------------
file(GLOB AQ_DIRECTXTEX_SOURCES CONFIGURE_DEPENDS "${AQ_THIRDPARTY_ROOT}/DirectXTex/*.cpp")

if(NOT WIN32)
	aq_exclude_sources(AQ_DIRECTXTEX_SOURCES
		"/DirectXTexWIC\\.cpp$"
		"/BCDirectCompute\\.cpp$"
		"/DirectXTexD3D1[12]\\.cpp$"
	)
endif()

add_library(directxtex STATIC ${AQ_DIRECTXTEX_SOURCES})
target_include_directories(directxtex PUBLIC "${AQ_THIRDPARTY_ROOT}")
target_compile_features(directxtex PRIVATE cxx_std_20)

# PUBLIC。DirectXTex の公開ヘッダ(DirectXTex.h)が XMFLOAT4 等と DXGI_FORMAT を
# 型に使うため、利用側(aqEngine → Game)も同じヘッダを見る必要がある。
target_link_libraries(directxtex PUBLIC directxmath directx-headers)

if(MSVC)
	# 第三者ソースなので警告レベルは下げる(vcxproj では /external:I 相当)
	target_compile_options(directxtex PRIVATE /utf-8 /W0)
endif()


# ----------------------------------------------------------------------------
#  imgui
#
#  imgui.vcxproj と同じソース集合。imgui_impl_dx12.cpp は同梱版がコア
#  (1.92 WIP)より新しく非互換のためビルドしない(DX12 の ImGui 描画は
#  aqEngine 側の D3D12ImGui.cpp が自前実装している)。
#  imgui_impl_win32.cpp / imgui_impl_dx11.cpp は Windows のみ。
#  Mac 用の imgui_impl_osx.mm 同梱は P4 の作業(設計書 §6)。
# ----------------------------------------------------------------------------
set(AQ_IMGUI_SOURCES
	"${AQ_THIRDPARTY_ROOT}/imgui/imgui.cpp"
	"${AQ_THIRDPARTY_ROOT}/imgui/imgui_draw.cpp"
	"${AQ_THIRDPARTY_ROOT}/imgui/imgui_tables.cpp"
	"${AQ_THIRDPARTY_ROOT}/imgui/imgui_widgets.cpp"
)

if(WIN32)
	list(APPEND AQ_IMGUI_SOURCES
		"${AQ_THIRDPARTY_ROOT}/imgui/imgui_impl_win32.cpp"
		"${AQ_THIRDPARTY_ROOT}/imgui/imgui_impl_dx11.cpp"
	)
endif()

add_library(imgui STATIC ${AQ_IMGUI_SOURCES})
target_include_directories(imgui PUBLIC "${AQ_THIRDPARTY_ROOT}")
target_compile_features(imgui PRIVATE cxx_std_20)

if(MSVC)
	target_compile_options(imgui PRIVATE /utf-8 /W0)
endif()


# ----------------------------------------------------------------------------
#  ufbx / spirv_reflect(どちらも C。可搬)
# ----------------------------------------------------------------------------
add_library(ufbx STATIC "${AQ_THIRDPARTY_ROOT}/ufbx/ufbx.c")
target_include_directories(ufbx PUBLIC "${AQ_THIRDPARTY_ROOT}")

add_library(spirv_reflect STATIC "${AQ_THIRDPARTY_ROOT}/spirv_reflect/spirv_reflect.c")
target_include_directories(spirv_reflect
	PUBLIC  "${AQ_THIRDPARTY_ROOT}"
	PRIVATE "${AQ_THIRDPARTY_ROOT}/spirv_reflect/include"
)

if(MSVC)
	target_compile_options(ufbx PRIVATE /W0)
	target_compile_options(spirv_reflect PRIVATE /W0)
endif()


# ----------------------------------------------------------------------------
#  vma(Vulkan Memory Allocator。ヘッダオンリ)
#
#  実装は aqEngine/Graphics/Vulkan/VulkanVMAImpl.cpp が
#  VMA_IMPLEMENTATION 付きで取り込む。
# ----------------------------------------------------------------------------
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE "${AQ_THIRDPARTY_ROOT}")


# ----------------------------------------------------------------------------
#  Bullet Physics
#
#  Windows: 既存の prebuilt .lib(ThirdParty/BulletPhysics/lib/Debug|Release)を
#           そのまま使う。vcxproj と同じ成果物を使うことで挙動差を作らない。
#  非 Windows: ソースからビルドする。
#
#  ソースビルドでは Bullet 同梱のルート CMakeLists.txt ではなく src/ を直接
#  add_subdirectory する。ルート側は cmake_minimum_required(VERSION 2.4.3) を
#  宣言していて CMake 4 系では読めず、デモ/Extras/UnitTests まで抱き込むため。
#  src/CMakeLists.txt は
#    BUILD_BULLET3=OFF … Bullet3* を除外
#    INSTALL_LIBS=OFF  … install ルールを出さない
#  としたうえで BulletInverseDynamics / BulletSoftBody / BulletCollision /
#  BulletDynamics / LinearMath / Bullet3Common を生成する。
#  BULLET_VERSION は各サブディレクトリの SET_TARGET_PROPERTIES が参照するため
#  ルート側と同じく VERSION ファイルから読んで与える。
# ----------------------------------------------------------------------------
set(AQ_BULLET_ROOT "${AQ_THIRDPARTY_ROOT}/BulletPhysics")

add_library(bullet INTERFACE)
target_include_directories(bullet INTERFACE "${AQ_BULLET_ROOT}/src")

# 既存 vcxproj の PreprocessorDefinitions と同じ。Bullet 本体と利用側の
# btScalar のサイズ・スレッド安全性を一致させるため PUBLIC 相当で伝播させる。
target_compile_definitions(bullet INTERFACE
	BT_USE_DOUBLE_PRECISION
	BT_THREADSAFE=1
)

if(WIN32)
	foreach(bulletLib BulletCollision BulletDynamics LinearMath)
		add_library(bullet_${bulletLib} STATIC IMPORTED GLOBAL)
		set_target_properties(bullet_${bulletLib} PROPERTIES
			IMPORTED_LOCATION         "${AQ_BULLET_ROOT}/lib/Release/${bulletLib}.lib"
			IMPORTED_LOCATION_DEBUG   "${AQ_BULLET_ROOT}/lib/Debug/${bulletLib}.lib"
			IMPORTED_LOCATION_RELEASE "${AQ_BULLET_ROOT}/lib/Release/${bulletLib}.lib"
		)
		target_link_libraries(bullet INTERFACE bullet_${bulletLib})
	endforeach()
else()
	file(STRINGS "${AQ_BULLET_ROOT}/VERSION" BULLET_VERSION)
	set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
	set(INSTALL_LIBS  OFF CACHE BOOL "" FORCE)
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

	add_subdirectory("${AQ_BULLET_ROOT}/src" bullet_src)

	foreach(bulletLib BulletCollision BulletDynamics LinearMath)
		target_compile_definitions(${bulletLib} PUBLIC BT_USE_DOUBLE_PRECISION BT_THREADSAFE=1)
	endforeach()

	target_link_libraries(bullet INTERFACE BulletDynamics BulletCollision LinearMath)
endif()
