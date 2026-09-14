# ============================================================================
#  .app バンドルへの同梱(Mac移植設計.md §9 P5 / iOS移植設計.md §7.3 の P2)
#
#  ビルド済みの Game.app へ、単体で起動するのに必要なものを投入する。
#  macOS と iOS の両方を扱い、AQ_PKG_PLATFORM で切り替える。
#
#  macOS(AQ_PKG_PLATFORM=macOS。従来):
#      Game.app/Contents/Resources/Game/Assets/...        ゲームアセット一式
#      Game.app/Contents/Frameworks/libvulkan.1.dylib     Vulkan ローダー
#      Game.app/Contents/Frameworks/libMoltenVK.dylib     ICD 本体
#      Game.app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json   ICD 定義
#
#      (Frameworks / icd.d は Vulkan 構成のときだけ。Metal 構成はシステム
#       フレームワークしか使わないので Assets だけで自己完結する)
#
#  iOS(AQ_PKG_PLATFORM=iOS):
#      Game.app/Game/Assets/...                           ゲームアセット一式
#
#  ------------------------------------------------------------------------
#  **iOS のバンドルは Contents/ 階層を持たない** ★macOS との最大の構造差
#
#  macOS の .app は Contents/{MacOS,Resources,Frameworks} という入れ子だが、
#  iOS の .app は **実行ファイルもリソースもバンドル直下**に並ぶ。
#  したがって同じ「リソースの置き場」でも
#
#      macOS:  Game.app/Contents/Resources/Game/Assets/...
#      iOS:    Game.app/Game/Assets/...
#
#  となる(設計書 iOS移植設計.md §7.3)。このスクリプトでは
#  AQ_PKG_RESOURCES_DIR がその差を吸収し、以降の処理は共通にしてある。
#  GetContentRoot() が返すのも、macOS は Contents/Resources、iOS は
#  バンドル直下(= [[NSBundle mainBundle] bundlePath])になる。
#
#  ------------------------------------------------------------------------
#  **Resources 直下ではなく <root>/Game/Assets に置く**(macOS / iOS 共通)
#
#  リソースのパス解決(aqEngine/Resource/Resource.cpp の
#  BuildResourcePathCandidates)は "Assets/..." を **<root>/Game/Assets/...**
#  へ組み立てる。<root> は UWP と同じく GetContentRoot()
#  (macOS: Contents/Resources、iOS: バンドル直下)。
#  つまりソースツリーの Game/ 1 段ぶんを再現する必要がある。
#  UWP の appx も同じ理由で install/Game/Assets/... に置いている
#  (Game/GameUWP.vcxproj の None Include の Link を参照)。Android の展開先も同じ。
#
#  ------------------------------------------------------------------------
#  使い方(Game/CMakeLists.txt の aqBundleApp ターゲットから -P で叩く)
#
#      cmake -D AQ_PKG_BUNDLE_DIR=<...>/Game.app \
#            -D AQ_PKG_ASSETS_DIR=<repo>/DirectX/Game/Assets \
#            -D AQ_PKG_GRAPHICS_API=Vulkan \
#            -D AQ_PKG_VULKAN_SDK=$VULKAN_SDK \
#            -P DirectX/Tools/PackageApp/package_app.cmake
#
#      cmake -D AQ_PKG_BUNDLE_DIR=<...>/Game.app \
#            -D AQ_PKG_ASSETS_DIR=<repo>/DirectX/Game/Assets \
#            -D AQ_PKG_GRAPHICS_API=Metal \
#            -D AQ_PKG_PLATFORM=iOS \
#            -P DirectX/Tools/PackageApp/package_app.cmake
#
#  変数:
#    AQ_PKG_BUNDLE_DIR    投入先の .app(必須)
#    AQ_PKG_ASSETS_DIR    同梱する Assets ディレクトリ(必須)
#    AQ_PKG_GRAPHICS_API  Vulkan / Metal(必須)
#    AQ_PKG_PLATFORM      macOS / iOS(任意。既定 macOS)。バンドル構造と
#                         シェーダのサブディレクトリ、Vulkan 同梱の有無を切り替える
#    AQ_PKG_VULKAN_SDK    Vulkan SDK のルート(Vulkan 構成のみ必須)
#    AQ_PKG_EXECUTABLE    実行ファイル。指定すると SDK の rpath を剥がして
#                         バンドル内の Frameworks だけを見るようにする(任意。
#                         Vulkan 構成 = macOS のみ有効)
#
#  コード署名は範囲外(設計書 Mac移植設計.md §9 P5)。
#  iOS 実機では **署名の後にバンドルを書き換えると署名が壊れる**ため、
#  aqBundleApp は署名より前に走らせる必要がある(P5 の課題。
#  Game/CMakeLists.txt の aqBundleApp のコメントを参照)。
# ============================================================================
cmake_minimum_required(VERSION 3.21)

if(NOT AQ_PKG_BUNDLE_DIR)
	message(FATAL_ERROR "AQ_PKG_BUNDLE_DIR が未指定です")
endif()
if(NOT IS_DIRECTORY "${AQ_PKG_BUNDLE_DIR}")
	message(FATAL_ERROR "バンドルがありません: ${AQ_PKG_BUNDLE_DIR}(先に Game をビルドしてください)")
endif()
if(NOT AQ_PKG_ASSETS_DIR OR NOT IS_DIRECTORY "${AQ_PKG_ASSETS_DIR}")
	message(FATAL_ERROR "AQ_PKG_ASSETS_DIR が不正です: ${AQ_PKG_ASSETS_DIR}")
endif()
if(NOT AQ_PKG_GRAPHICS_API MATCHES "^(Vulkan|Metal)$")
	message(FATAL_ERROR "AQ_PKG_GRAPHICS_API は Vulkan / Metal のいずれかです(現在: ${AQ_PKG_GRAPHICS_API})")
endif()
if(NOT AQ_PKG_PLATFORM)
	# 未指定なら従来どおり macOS 扱い(既存の呼び出しを変えないため)。
	set(AQ_PKG_PLATFORM "macOS")
endif()
if(NOT AQ_PKG_PLATFORM MATCHES "^(macOS|iOS)$")
	message(FATAL_ERROR "AQ_PKG_PLATFORM は macOS / iOS のいずれかです(現在: ${AQ_PKG_PLATFORM})")
endif()

# iOS は Metal 一本(設計書 iOS移植設計.md §0.3)。MoltenVK を .ipa へ同梱する
# 道は採らないと決めているので、取り違えを早い段階で止める。
if(AQ_PKG_PLATFORM STREQUAL "iOS" AND AQ_PKG_GRAPHICS_API STREQUAL "Vulkan")
	message(FATAL_ERROR
		"iOS は Metal 一本です(設計書 iOS移植設計.md §0.3)。"
		"AQ_PKG_GRAPHICS_API=Vulkan では iOS のバンドルを作れません。")
endif()


# ----------------------------------------------------------------------------
#  バンドル内のレイアウト
#
#  **iOS の .app は Contents/ 階層を持たない**(リソースはバンドル直下)。
#  これが macOS との最大の構造差なので、ここで吸収して以降を共通にする。
#  Frameworks の位置も本来は違うが、iOS では Vulkan 分岐ごと通らないため
#  実際には使われない(下の 3. を参照)。
# ----------------------------------------------------------------------------
if(AQ_PKG_PLATFORM STREQUAL "iOS")
	set(AQ_PKG_CONTENTS_DIR   "${AQ_PKG_BUNDLE_DIR}")
	# バンドル直下ではなく Content/ 1 段を挟む。フラットバンドルでは
	# **実行ファイル自体が <Bundle>/Game** で、アセット側が要求する <root>/Game/ と
	# 名前が衝突するため(実測: file(MAKE_DIRECTORY) が "File exists" で落ちる)。
	# バンドル直下は Info.plist / PkgInfo / _CodeSignature も置かれる OS 側の
	# 名前空間なので、こちらの持ち物はまとめて Content/ に隔離する。
	# 読み出し側は aqEngine/Platform/iOS/PlatformiOS.mm の GetContentRoot()。
	# **片方だけ変えないこと。**
	set(AQ_PKG_RESOURCES_DIR  "${AQ_PKG_BUNDLE_DIR}/Content")
	set(AQ_PKG_FRAMEWORKS_DIR "${AQ_PKG_BUNDLE_DIR}/Frameworks")
else()
	set(AQ_PKG_CONTENTS_DIR   "${AQ_PKG_BUNDLE_DIR}/Contents")
	set(AQ_PKG_RESOURCES_DIR  "${AQ_PKG_CONTENTS_DIR}/Resources")
	set(AQ_PKG_FRAMEWORKS_DIR "${AQ_PKG_CONTENTS_DIR}/Frameworks")
endif()


# ----------------------------------------------------------------------------
#  1. シェーダ生成物の存在確認
#
#  Assets/Shader/msl(macOS Metal)/ msl-ios(iOS Metal)/ spv(Vulkan)は
#  .gitignore 済みのビルド生成物で、これが無いとバンドルは起動できても
#  シェーダ作成で落ちる。aqBundleApp は Game 経由で aqCompileMsl / aqCompileSpv に
#  依存しているので通常は存在するが、手で -P を叩いたときのために確認しておく。
#
#  iOS 版 MSL は spirv-cross の --msl-ios で作った別物で、macOS 版とは
#  ディレクトリごと分かれている(Tools/ShaderCompile/compile_msl.cmake)。
# ----------------------------------------------------------------------------
if(AQ_PKG_GRAPHICS_API STREQUAL "Metal")
	if(AQ_PKG_PLATFORM STREQUAL "iOS")
		set(AQ_PKG_SHADER_SUBDIR "msl-ios")
	else()
		set(AQ_PKG_SHADER_SUBDIR "msl")
	endif()
	set(AQ_PKG_SHADER_TARGET "aqCompileMsl")
else()
	set(AQ_PKG_SHADER_SUBDIR "spv")
	set(AQ_PKG_SHADER_TARGET "aqCompileSpv")
endif()

if(NOT IS_DIRECTORY "${AQ_PKG_ASSETS_DIR}/Shader/${AQ_PKG_SHADER_SUBDIR}")
	message(FATAL_ERROR
		"シェーダ生成物がありません: ${AQ_PKG_ASSETS_DIR}/Shader/${AQ_PKG_SHADER_SUBDIR}\n"
		"先に ${AQ_PKG_SHADER_TARGET} をビルドしてください。")
endif()


# ----------------------------------------------------------------------------
#  2. Assets
#
#  file(COPY) は「同じタイムスタンプのファイルはコピーしない」ので、2 回目以降は
#  差分だけになる(Assets は 92MB あるため毎回の全コピーは避けたい)。
# ----------------------------------------------------------------------------
message(STATUS "aqBundleApp: Assets -> ${AQ_PKG_RESOURCES_DIR}/Game/Assets")
file(MAKE_DIRECTORY "${AQ_PKG_RESOURCES_DIR}/Game")
file(COPY "${AQ_PKG_ASSETS_DIR}" DESTINATION "${AQ_PKG_RESOURCES_DIR}/Game")


# ----------------------------------------------------------------------------
#  3. Vulkan のランタイム
#
#  実行ファイルは @rpath/libvulkan.1.dylib を参照しているので、ローダーを
#  Frameworks へ置いて rpath に @executable_path/../Frameworks を通す
#  (rpath は Game/CMakeLists.txt の BUILD_RPATH / INSTALL_RPATH 側で付ける)。
#
#  ICD(libMoltenVK.dylib)はローダーが JSON 経由で dlopen するだけなので
#  rpath とは無関係。ただし SDK の JSON は library_path が
#  "../../../lib/libMoltenVK.dylib"(= SDK のレイアウト前提)なので、
#  **バンドル内のレイアウトに合わせて書き換える**。ローダーは相対の
#  library_path を「JSON のあるディレクトリからの相対」として解決する。
#
#      <Contents>/Resources/vulkan/icd.d/MoltenVK_icd.json
#        ../../.. = <Contents> → ../../../Frameworks/libMoltenVK.dylib
#
#  **iOS ではこのブロックを丸ごとスキップする**(Metal 一本なのでローダーも ICD も
#  要らず、install_name_tool による rpath の剥がしも不要。相対パスの前提も
#  Contents/ がある macOS のレイアウト依存で、iOS には当てはまらない)。
#  上の FATAL_ERROR で iOS + Vulkan は弾いてあるが、条件にも明示しておく。
# ----------------------------------------------------------------------------
if(AQ_PKG_GRAPHICS_API STREQUAL "Vulkan" AND AQ_PKG_PLATFORM STREQUAL "macOS")
	if(NOT AQ_PKG_VULKAN_SDK OR NOT IS_DIRECTORY "${AQ_PKG_VULKAN_SDK}")
		message(FATAL_ERROR "AQ_PKG_VULKAN_SDK が不正です: ${AQ_PKG_VULKAN_SDK}")
	endif()

	set(AQ_PKG_LOADER   "${AQ_PKG_VULKAN_SDK}/lib/libvulkan.1.dylib")
	set(AQ_PKG_ICD_LIB  "${AQ_PKG_VULKAN_SDK}/lib/libMoltenVK.dylib")
	set(AQ_PKG_ICD_JSON "${AQ_PKG_VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json")
	foreach(aqPkgFile "${AQ_PKG_LOADER}" "${AQ_PKG_ICD_LIB}" "${AQ_PKG_ICD_JSON}")
		if(NOT EXISTS "${aqPkgFile}")
			message(FATAL_ERROR "Vulkan SDK に見当たりません: ${aqPkgFile}")
		endif()
	endforeach()

	# dylib は実体をコピーする(libvulkan.1.dylib は SDK ではシンボリックリンク。
	# file(COPY) だとリンクのまま複製されて配布先で切れるため COPY_FILE を使う)。
	message(STATUS "aqBundleApp: Vulkan ランタイム -> ${AQ_PKG_FRAMEWORKS_DIR}")
	file(MAKE_DIRECTORY "${AQ_PKG_FRAMEWORKS_DIR}")
	file(COPY_FILE "${AQ_PKG_LOADER}"  "${AQ_PKG_FRAMEWORKS_DIR}/libvulkan.1.dylib"  ONLY_IF_DIFFERENT)
	file(COPY_FILE "${AQ_PKG_ICD_LIB}" "${AQ_PKG_FRAMEWORKS_DIR}/libMoltenVK.dylib" ONLY_IF_DIFFERENT)

	# ICD 定義。library_path をバンドル内の相対パスへ差し替える。
	file(READ "${AQ_PKG_ICD_JSON}" aqPkgIcdText)
	string(REGEX REPLACE
		"\"library_path\"[ \t]*:[ \t]*\"[^\"]*\""
		"\"library_path\": \"../../../Frameworks/libMoltenVK.dylib\""
		aqPkgIcdText "${aqPkgIcdText}")
	if(NOT aqPkgIcdText MATCHES "\\.\\./\\.\\./\\.\\./Frameworks/libMoltenVK\\.dylib")
		message(FATAL_ERROR "MoltenVK_icd.json の library_path を書き換えられませんでした: ${AQ_PKG_ICD_JSON}")
	endif()
	file(MAKE_DIRECTORY "${AQ_PKG_RESOURCES_DIR}/vulkan/icd.d")
	file(WRITE "${AQ_PKG_RESOURCES_DIR}/vulkan/icd.d/MoltenVK_icd.json" "${aqPkgIcdText}")
	message(STATUS "aqBundleApp: ICD 定義 -> ${AQ_PKG_RESOURCES_DIR}/vulkan/icd.d/MoltenVK_icd.json")

	# 実行ファイルに残っている SDK の rpath を剥がす。
	# これが残っていると「バンドルに同梱した dylib ではなく、開発機の SDK を
	# 読んでいるだけ」という状態を取り違える(配布先では SDK が無いので壊れる)。
	# 既に剥がれている場合は install_name_tool が失敗するが、実害は無いので握る。
	if(AQ_PKG_EXECUTABLE AND EXISTS "${AQ_PKG_EXECUTABLE}")
		execute_process(
			COMMAND install_name_tool -delete_rpath "${AQ_PKG_VULKAN_SDK}/lib" "${AQ_PKG_EXECUTABLE}"
			RESULT_VARIABLE aqPkgRpathResult
			OUTPUT_QUIET
			ERROR_QUIET
		)
		if(aqPkgRpathResult EQUAL 0)
			message(STATUS "aqBundleApp: SDK の rpath を削除しました(${AQ_PKG_VULKAN_SDK}/lib)")
		endif()
	endif()
endif()

message(STATUS "aqBundleApp: 完了 ${AQ_PKG_BUNDLE_DIR}(${AQ_PKG_PLATFORM} / ${AQ_PKG_GRAPHICS_API})")
