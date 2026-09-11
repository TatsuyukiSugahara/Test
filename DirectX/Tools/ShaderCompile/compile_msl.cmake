# ============================================================================
#  ビルド時 MSL 生成 (MetalBackend設計.md §9.1)
#
#  Game/Assets/Shader/shader_entries.txt に並んだ <file> <entry> <stage> を
#  2 段構成でコンパイルし、Game/Assets/Shader/msl/ へ
#      <stem>.<entry>.<stage>.spv    (中間。P2 の頂点入力リフレクション用に残す)
#      <stem>.<entry>.<stage>.metal  (実行時 newLibraryWithSource: へ渡す MSL)
#  という名前で出力する。
#
#      dxc          -spirv ... -E <entry> -T <stage>_6_0 -I <shaderDir> -Fo <out.spv> <src.fx>
#      spirv-cross  --msl --msl-version 20000 --msl-decoration-binding --output <out.metal> <out.spv>
#
#  出力名は aqEngine/Graphics/Metal/MetalShader.mm の BuildMslPath() が探すパスと
#  一対一で対応している。**片方だけ変えないこと** (compile_spv.cmake と
#  VulkanShader.cpp が同じ約束をしているのと同じ)。
#
#  エントリ一覧は Vulkan 用と同じ shader_entries.txt をそのまま再利用する
#  (新規ファイルを作らない。59 エントリ)。
#
#  出力先は **Vulkan 用の Game/Assets/Shader/spv/ とは別ディレクトリ**にする。
#  register -> binding のシフトが違う (Vulkan: b:0/t:16/s:32/u:48、
#  Metal: b:0/t:0/s:0/u:16) ので、同名でも中身がまったくの別物になる。
#  混ざると Vulkan 構成が壊れる。
#
#  dxc へ渡す固定引数は Tools/ShaderCompile/dxc_args_metal.txt を単一ソースとする。
#  Metal には実行時 DXC 経路が無い (実行時にコンパイルするのは MSL であって HLSL では
#  ない) ので、Vulkan の VulkanShader.cpp のような #include 側は存在しない。
#
#  TODO(P5): ClusterCull.main.cs は spirv-cross 自体は成功するが、生成 MSL が
#    アドレス空間をまたぐキャストを含み newLibraryWithSource: が
#    "converts between mismatching address spaces" で落ちる (設計書 §13-1)。
#    ここでは特別扱いせず普通に生成する。対処は P5 で決める。
#
#  ------------------------------------------------------------------------
#  使い方 A: 単体スクリプトとして実行する (-D は -P より前に置くこと)
#
#      cmake -D AQ_MSL_DXC=~/VulkanSDK/1.4.357.1/macOS/bin/dxc \
#            -D AQ_MSL_SPIRV_CROSS=~/VulkanSDK/1.4.357.1/macOS/bin/spirv-cross \
#            -P DirectX/Tools/ShaderCompile/compile_msl.cmake
#
#  使い方 B: CMakeLists.txt から include して生成ターゲットを足す
#
#      include(${CMAKE_CURRENT_SOURCE_DIR}/Tools/ShaderCompile/compile_msl.cmake)
#      aq_add_compile_msl_target(aqCompileMsl)
#      add_dependencies(Game aqCompileMsl)
#
#  変数 (すべて任意。未指定なら既定値):
#    AQ_MSL_DXC          dxc の実行ファイル。既定は $ENV{VULKAN_SDK} 配下 → PATH の順に探索
#    AQ_MSL_SPIRV_CROSS  spirv-cross の実行ファイル。既定は同上 (Vulkan SDK 同梱)
#    AQ_MSL_SHADER_DIR   .fx の置き場。既定 <repo>/DirectX/Game/Assets/Shader
#    AQ_MSL_ENTRIES      エントリ一覧。既定 ${AQ_MSL_SHADER_DIR}/shader_entries.txt
#    AQ_MSL_OUT_DIR      .spv / .metal の出力先。既定 ${AQ_MSL_SHADER_DIR}/msl
#    AQ_MSL_ARGS_FILE    dxc 固定引数。既定 <このファイルの隣>/dxc_args_metal.txt
#    AQ_MSL_DEBUG_INFO   ON で AQ_DXC_ARG_DEBUG(...) の引数も渡す。既定 OFF
# ============================================================================

# include されたときに呼び出し元のポリシースコープを書き換えないよう、
# cmake -P で直接叩かれたときだけ宣言する。
if(CMAKE_SCRIPT_MODE_FILE)
	cmake_minimum_required(VERSION 3.21)
endif()

set(AQ_MSL_LIST_DIR "${CMAKE_CURRENT_LIST_DIR}")


# ----------------------------------------------------------------------------
#  dxc_args_metal.txt を読み、固定引数リストとシェーダモデルを取り出す
#
#  行頭 "//" の行は読み飛ばす。AQ_DXC_ARG("x") / AQ_DXC_ARG_DEBUG("x") の
#  ダブルクォート内をそのまま 1 引数として拾う。
# ----------------------------------------------------------------------------
function(aq_msl_read_args argsFile withDebug outArgs outModel)
	if(NOT EXISTS "${argsFile}")
		message(FATAL_ERROR "dxc 引数ファイルが見つかりません: ${argsFile}")
	endif()

	file(STRINGS "${argsFile}" lines ENCODING UTF-8)

	set(args "")
	set(model "")
	foreach(line IN LISTS lines)
		# シェーダモデル指定 (コメント行に書いてあるので読み飛ばしより先に判定する)
		if(line MATCHES "AQ_DXC_SHADER_MODEL[ \t]+([0-9]+_[0-9]+)")
			set(model "${CMAKE_MATCH_1}")
		endif()

		# コメント行は無視 (コメントアウトした引数を拾わないため)
		if(line MATCHES "^[ \t]*//")
			continue()
		endif()

		string(REGEX MATCHALL "AQ_DXC_ARG[ \t]*\\(\"[^\"]*\"\\)" hits "${line}")
		if(withDebug)
			string(REGEX MATCHALL "AQ_DXC_ARG_DEBUG[ \t]*\\(\"[^\"]*\"\\)" dbgHits "${line}")
			list(APPEND hits ${dbgHits})
		endif()

		foreach(hit IN LISTS hits)
			string(REGEX REPLACE "^[^\"]*\"(.*)\"[^\"]*$" "\\1" arg "${hit}")
			list(APPEND args "${arg}")
		endforeach()
	endforeach()

	if(args STREQUAL "")
		message(FATAL_ERROR "dxc 引数を 1 つも読み取れませんでした: ${argsFile}")
	endif()
	if(model STREQUAL "")
		message(FATAL_ERROR "AQ_DXC_SHADER_MODEL の行がありません: ${argsFile}")
	endif()

	set(${outArgs}  "${args}"  PARENT_SCOPE)
	set(${outModel} "${model}" PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  shader_entries.txt を読み、"<file>|<entry>|<stage>" のリストへ変換する
#
#  Vulkan 用と同じファイルを共有しているので、書式の扱いも compile_spv.cmake と
#  完全に合わせる。"#" 以降は行コメント。空行は無視。
# ----------------------------------------------------------------------------
function(aq_msl_read_entries entriesFile outEntries)
	if(NOT EXISTS "${entriesFile}")
		message(FATAL_ERROR "エントリ一覧が見つかりません: ${entriesFile}")
	endif()

	file(STRINGS "${entriesFile}" lines ENCODING UTF-8)

	set(entries "")
	foreach(line IN LISTS lines)
		string(REGEX REPLACE "#.*$" "" line "${line}")
		string(STRIP "${line}" line)
		if(line STREQUAL "")
			continue()
		endif()

		string(REGEX MATCHALL "[^ \t]+" tokens "${line}")
		list(LENGTH tokens tokenCount)
		if(NOT tokenCount EQUAL 3)
			message(FATAL_ERROR "shader_entries.txt の書式が不正です (<file> <entry> <stage>): ${line}")
		endif()

		list(GET tokens 0 fx)
		list(GET tokens 1 entry)
		list(GET tokens 2 stage)
		if(NOT stage MATCHES "^(vs|ps|cs)$")
			message(FATAL_ERROR "shader_entries.txt の stage が不正です (vs|ps|cs): ${line}")
		endif()

		list(APPEND entries "${fx}|${entry}|${stage}")
	endforeach()

	set(${outEntries} "${entries}" PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  Vulkan SDK 配下の探索ヒント (dxc / spirv-cross で共通)
# ----------------------------------------------------------------------------
function(aq_msl_sdk_hints outHints)
	set(hints "")
	if(DEFINED ENV{VULKAN_SDK})
		list(APPEND hints "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/macOS/bin")
	endif()
	set(${outHints} "${hints}" PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  dxc の実行ファイルを解決する
# ----------------------------------------------------------------------------
function(aq_msl_resolve_dxc outPath)
	if(AQ_MSL_DXC)
		set(${outPath} "${AQ_MSL_DXC}" PARENT_SCOPE)
		return()
	endif()

	# Windows SDK 同梱の dxc.exe は -spirv 非対応なので、Vulkan SDK を優先して探す。
	aq_msl_sdk_hints(hints)

	find_program(AQ_MSL_DXC_FOUND
		NAMES dxc dxc.exe
		HINTS ${hints}
		NO_CACHE
	)
	if(NOT AQ_MSL_DXC_FOUND)
		message(FATAL_ERROR
			"dxc が見つかりません。Vulkan SDK を導入して VULKAN_SDK を設定するか、"
			"-D AQ_MSL_DXC=<dxc のパス> を指定してください。")
	endif()

	set(${outPath} "${AQ_MSL_DXC_FOUND}" PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  spirv-cross の実行ファイルを解決する
#
#  spirv-cross は **Vulkan SDK に同梱**されている (macOS なら
#  $VULKAN_SDK/macOS/bin/spirv-cross)。単体で入れるものではないので、
#  見つからないときはその旨が分かる文言で止める。
# ----------------------------------------------------------------------------
function(aq_msl_resolve_spirv_cross outPath)
	if(AQ_MSL_SPIRV_CROSS)
		set(${outPath} "${AQ_MSL_SPIRV_CROSS}" PARENT_SCOPE)
		return()
	endif()

	aq_msl_sdk_hints(hints)

	find_program(AQ_MSL_SPIRV_CROSS_FOUND
		NAMES spirv-cross spirv-cross.exe
		HINTS ${hints}
		NO_CACHE
	)
	if(NOT AQ_MSL_SPIRV_CROSS_FOUND)
		message(FATAL_ERROR
			"spirv-cross が見つかりません。これは **Vulkan SDK に同梱**されている実行ファイルです "
			"(macOS なら $VULKAN_SDK/macOS/bin/spirv-cross)。Vulkan SDK を導入して "
			"VULKAN_SDK を設定するか、-D AQ_MSL_SPIRV_CROSS=<spirv-cross のパス> を指定してください。")
	endif()

	set(${outPath} "${AQ_MSL_SPIRV_CROSS_FOUND}" PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  既定値の解決 (スクリプト実行/include のどちらでも使う)
# ----------------------------------------------------------------------------
macro(aq_msl_resolve_defaults)
	if(NOT AQ_MSL_SHADER_DIR)
		# Tools/ShaderCompile -> DirectX -> Game/Assets/Shader
		get_filename_component(AQ_MSL_SHADER_DIR "${AQ_MSL_LIST_DIR}/../../Game/Assets/Shader" ABSOLUTE)
	endif()
	if(NOT AQ_MSL_ENTRIES)
		# Vulkan 用と同じファイルを再利用する (新規に作らない)。
		set(AQ_MSL_ENTRIES "${AQ_MSL_SHADER_DIR}/shader_entries.txt")
	endif()
	if(NOT AQ_MSL_OUT_DIR)
		# Vulkan の spv/ とは分ける (シフトが違うので中身が別物)。
		set(AQ_MSL_OUT_DIR "${AQ_MSL_SHADER_DIR}/msl")
	endif()
	if(NOT AQ_MSL_ARGS_FILE)
		set(AQ_MSL_ARGS_FILE "${AQ_MSL_LIST_DIR}/dxc_args_metal.txt")
	endif()
	if(NOT DEFINED AQ_MSL_DEBUG_INFO)
		set(AQ_MSL_DEBUG_INFO OFF)
	endif()
endmacro()


# ----------------------------------------------------------------------------
#  1 エントリを .spv -> .metal の 2 段でコンパイルする
#
#  失敗しても即 FATAL_ERROR にはせず、呼び出し元が件数を数えられるよう
#  SEND_ERROR + outOk で返す (1 本目で止まると全体の状況が分からないため)。
# ----------------------------------------------------------------------------
function(aq_msl_compile_one dxc spirvCross commonArgs shaderModel fx entry stage outOk)
	set(${outOk} FALSE PARENT_SCOPE)

	get_filename_component(stem "${fx}" NAME_WE)
	set(src     "${AQ_MSL_SHADER_DIR}/${fx}")
	set(spvOut  "${AQ_MSL_OUT_DIR}/${stem}.${entry}.${stage}.spv")
	set(mslOut  "${AQ_MSL_OUT_DIR}/${stem}.${entry}.${stage}.metal")

	if(NOT EXISTS "${src}")
		message(SEND_ERROR "[compile_msl] .fx がありません: ${src}")
		return()
	endif()

	# 1 段目: HLSL -> SPIR-V (Metal 用シフト。dxc_args_metal.txt が単一ソース)
	execute_process(
		COMMAND "${dxc}"
		        ${commonArgs}
		        -E "${entry}"
		        -T "${stage}_${shaderModel}"
		        -I "${AQ_MSL_SHADER_DIR}"
		        -Fo "${spvOut}"
		        "${src}"
		RESULT_VARIABLE rc
		OUTPUT_VARIABLE stdOut
		ERROR_VARIABLE  stdErr
	)
	if(NOT rc EQUAL 0)
		message(SEND_ERROR "[compile_msl] dxc 失敗: ${fx} ${entry} (${stage})\n${stdOut}${stdErr}")
		return()
	elseif(NOT stdErr STREQUAL "")
		message(STATUS "[compile_msl] dxc 警告: ${fx} ${entry} (${stage})\n${stdErr}")
	endif()

	# 2 段目: SPIR-V -> MSL
	#   --msl-decoration-binding … SPIR-V の binding をそのまま Metal の index にする
	#                              (実行時のリフレクション / 写像テーブルが要らなくなる。設計書 §5.1)
	#   --output                  … stdout リダイレクトを使わずファイルへ直接書く
	execute_process(
		COMMAND "${spirvCross}"
		        --msl
		        --msl-version 20000
		        --msl-decoration-binding
		        --output "${mslOut}"
		        "${spvOut}"
		RESULT_VARIABLE rc
		OUTPUT_VARIABLE stdOut
		ERROR_VARIABLE  stdErr
	)
	if(NOT rc EQUAL 0)
		message(SEND_ERROR "[compile_msl] spirv-cross 失敗: ${fx} ${entry} (${stage})\n${stdOut}${stdErr}")
		return()
	elseif(NOT stdErr STREQUAL "")
		message(STATUS "[compile_msl] spirv-cross 警告: ${fx} ${entry} (${stage})\n${stdErr}")
	endif()

	set(${outOk} TRUE PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  全エントリをコンパイルする (スクリプト実行の本体)
# ----------------------------------------------------------------------------
function(aq_msl_compile_all)
	aq_msl_resolve_defaults()
	aq_msl_resolve_dxc(dxc)
	aq_msl_resolve_spirv_cross(spirvCross)
	aq_msl_read_args("${AQ_MSL_ARGS_FILE}" "${AQ_MSL_DEBUG_INFO}" commonArgs shaderModel)
	aq_msl_read_entries("${AQ_MSL_ENTRIES}" entries)

	file(MAKE_DIRECTORY "${AQ_MSL_OUT_DIR}")

	list(LENGTH entries entryCount)
	message(STATUS "[compile_msl] dxc          = ${dxc}")
	message(STATUS "[compile_msl] spirv-cross  = ${spirvCross}")
	message(STATUS "[compile_msl] shaders      = ${AQ_MSL_SHADER_DIR}")
	message(STATUS "[compile_msl] out          = ${AQ_MSL_OUT_DIR}")
	message(STATUS "[compile_msl] entries      = ${entryCount}")

	set(failed 0)
	foreach(record IN LISTS entries)
		string(REPLACE "|" ";" parts "${record}")
		list(GET parts 0 fx)
		list(GET parts 1 entry)
		list(GET parts 2 stage)

		aq_msl_compile_one("${dxc}" "${spirvCross}" "${commonArgs}" "${shaderModel}"
		                   "${fx}" "${entry}" "${stage}" ok)
		if(NOT ok)
			math(EXPR failed "${failed}+1")
		endif()
	endforeach()

	if(NOT failed EQUAL 0)
		message(FATAL_ERROR "[compile_msl] ${failed} 件のシェーダがコンパイルできませんでした")
	endif()
	message(STATUS "[compile_msl] 完了 (${entryCount} 本)")
endfunction()


# ----------------------------------------------------------------------------
#  include されたとき用: 生成ターゲットを 1 つ足す
#
#  ビルドのたびに全エントリを再生成する (エントリ数 59 本)。
#  .fx は Common.fx / Lighting.fx などを #include するため、
#  ソース単体のタイムスタンプ比較では依存が拾えず、部分再生成は正しくない。
# ----------------------------------------------------------------------------
function(aq_add_compile_msl_target targetName)
	aq_msl_resolve_defaults()

	# dxc / spirv-cross は **configure 時に** 解決して絶対パスを焼き込む。
	# スクリプト側の探索は $ENV{VULKAN_SDK} を見るが、ビルドを起動する環境が
	# 必ずしもそれを持っているとは限らない。特に **Xcode はターミナルの環境を
	# 引き継がない**ため、ここで渡さないと ⌘B が
	# 「PhaseScriptExecution failed with a nonzero exit code」で落ちる。
	# Metal 構成でも configure 時に VULKAN_SDK を要求しているので確実に解決できる
	# (dxc / spirv-cross がどちらも Vulkan SDK 同梱のため。設計書 §11)。
	aq_msl_resolve_dxc(AQ_MSL_DXC)
	aq_msl_resolve_spirv_cross(AQ_MSL_SPIRV_CROSS)

	set(scriptArgs
		-D "AQ_MSL_SHADER_DIR=${AQ_MSL_SHADER_DIR}"
		-D "AQ_MSL_ENTRIES=${AQ_MSL_ENTRIES}"
		-D "AQ_MSL_OUT_DIR=${AQ_MSL_OUT_DIR}"
		-D "AQ_MSL_ARGS_FILE=${AQ_MSL_ARGS_FILE}"
		-D "AQ_MSL_DEBUG_INFO=${AQ_MSL_DEBUG_INFO}"
	)
	list(APPEND scriptArgs -D "AQ_MSL_DXC=${AQ_MSL_DXC}")
	list(APPEND scriptArgs -D "AQ_MSL_SPIRV_CROSS=${AQ_MSL_SPIRV_CROSS}")

	add_custom_target(${targetName}
		COMMAND ${CMAKE_COMMAND} ${scriptArgs} -P "${AQ_MSL_LIST_DIR}/compile_msl.cmake"
		COMMENT "MSL を生成中 (${AQ_MSL_ENTRIES})"
		VERBATIM
	)
endfunction()


# cmake -P で直接叩かれたときだけ実行する (include 時は関数定義だけ)
if(CMAKE_SCRIPT_MODE_FILE)
	get_filename_component(aqMslThisFile "${CMAKE_CURRENT_LIST_FILE}" ABSOLUTE)
	get_filename_component(aqMslScriptFile "${CMAKE_SCRIPT_MODE_FILE}" ABSOLUTE)
	if(aqMslThisFile STREQUAL aqMslScriptFile)
		aq_msl_compile_all()
	endif()
endif()
