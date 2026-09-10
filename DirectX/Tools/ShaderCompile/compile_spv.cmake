# ============================================================================
#  ビルド時 SPIR-V 生成 (Mac移植設計.md §4)
#
#  Game/Assets/Shader/shader_entries.txt に並んだ <file> <entry> <stage> を
#  dxc で 1 本ずつコンパイルし、Game/Assets/Shader/spv/ へ
#      <stem>.<entry>.<stage>.spv
#  という名前で出力する。出力名は aqEngine/Graphics/Vulkan/VulkanShader.cpp の
#  BuildSpirvPath() が探すパスと一対一で対応している。
#
#  dxc へ渡す固定引数は Tools/ShaderCompile/dxc_args.txt を単一ソースとする。
#  VulkanShader.cpp も同じファイルを #include して実行時コンパイルの引数に使うため、
#  「実行時に作った SPIR-V」と「ビルド時に作った .spv」で引数が食い違わない。
#
#  Mac では実行時 DXC 経路が無いので、この生成が必須になる。
#
#  ------------------------------------------------------------------------
#  使い方 A: 単体スクリプトとして実行する (-D は -P より前に置くこと)
#
#      cmake -D AQ_SPV_DXC=C:/VulkanSDK/1.4.350.0/Bin/dxc.exe \
#            -P DirectX/Tools/ShaderCompile/compile_spv.cmake
#
#  使い方 B: CMakeLists.txt から include して生成ターゲットを足す
#
#      include(${CMAKE_CURRENT_SOURCE_DIR}/Tools/ShaderCompile/compile_spv.cmake)
#      aq_add_compile_spv_target(aqCompileSpv)
#      add_dependencies(Game aqCompileSpv)
#
#  変数 (すべて任意。未指定なら既定値):
#    AQ_SPV_DXC         dxc の実行ファイル。既定は $ENV{VULKAN_SDK} 配下 → PATH の順に探索
#    AQ_SPV_SHADER_DIR  .fx の置き場。既定 <repo>/DirectX/Game/Assets/Shader
#    AQ_SPV_ENTRIES     エントリ一覧。既定 ${AQ_SPV_SHADER_DIR}/shader_entries.txt
#    AQ_SPV_OUT_DIR     .spv の出力先。既定 ${AQ_SPV_SHADER_DIR}/spv
#    AQ_SPV_ARGS_FILE   dxc 固定引数。既定 <このファイルの隣>/dxc_args.txt
#    AQ_SPV_DEBUG_INFO  ON で AQ_DXC_ARG_DEBUG(...) の引数も渡す。既定 OFF
#
#  TODO(Mac実機): 要確認 — この環境に Vulkan SDK / dxc が無く、実際のコンパイルは未検証。
#    (1) SPIR-V 出力に -Fo <out.spv> が効くこと (dxc -spirv の正規の出力指定)。
#    (2) 実行時経路は CWD をシェーダディレクトリへ移して #include を解決しているが、
#        ここではソースをパス指定するため dxc がソース相対で解決する前提にしている
#        (-I <shaderDir> も併せて渡している)。Common.fx / Lighting.fx 等が引けること。
#    (3) macOS の Vulkan SDK での dxc の実際の配置 ($VULKAN_SDK/bin か macOS/bin か)。
# ============================================================================

# include されたときに呼び出し元のポリシースコープを書き換えないよう、
# cmake -P で直接叩かれたときだけ宣言する。
if(CMAKE_SCRIPT_MODE_FILE)
	cmake_minimum_required(VERSION 3.21)
endif()

set(AQ_SPV_LIST_DIR "${CMAKE_CURRENT_LIST_DIR}")


# ----------------------------------------------------------------------------
#  dxc_args.txt を読み、固定引数リストとシェーダモデルを取り出す
#
#  行頭 "//" の行は読み飛ばす。AQ_DXC_ARG("x") / AQ_DXC_ARG_DEBUG("x") の
#  ダブルクォート内をそのまま 1 引数として拾う。
# ----------------------------------------------------------------------------
function(aq_spv_read_args argsFile withDebug outArgs outModel)
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
#  "#" 以降は行コメント。空行は無視。
# ----------------------------------------------------------------------------
function(aq_spv_read_entries entriesFile outEntries)
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
#  dxc の実行ファイルを解決する
# ----------------------------------------------------------------------------
function(aq_spv_resolve_dxc outPath)
	if(AQ_SPV_DXC)
		set(${outPath} "${AQ_SPV_DXC}" PARENT_SCOPE)
		return()
	endif()

	# Windows SDK 同梱の dxc.exe は -spirv 非対応なので、Vulkan SDK を優先して探す。
	set(hints "")
	if(DEFINED ENV{VULKAN_SDK})
		list(APPEND hints "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/macOS/bin")
	endif()

	find_program(AQ_SPV_DXC_FOUND
		NAMES dxc dxc.exe
		HINTS ${hints}
		NO_CACHE
	)
	if(NOT AQ_SPV_DXC_FOUND)
		message(FATAL_ERROR
			"dxc が見つかりません。Vulkan SDK を導入して VULKAN_SDK を設定するか、"
			"-D AQ_SPV_DXC=<dxc のパス> を指定してください。")
	endif()

	set(${outPath} "${AQ_SPV_DXC_FOUND}" PARENT_SCOPE)
endfunction()


# ----------------------------------------------------------------------------
#  既定値の解決 (スクリプト実行/include のどちらでも使う)
# ----------------------------------------------------------------------------
macro(aq_spv_resolve_defaults)
	if(NOT AQ_SPV_SHADER_DIR)
		# Tools/ShaderCompile -> DirectX -> Game/Assets/Shader
		get_filename_component(AQ_SPV_SHADER_DIR "${AQ_SPV_LIST_DIR}/../../Game/Assets/Shader" ABSOLUTE)
	endif()
	if(NOT AQ_SPV_ENTRIES)
		set(AQ_SPV_ENTRIES "${AQ_SPV_SHADER_DIR}/shader_entries.txt")
	endif()
	if(NOT AQ_SPV_OUT_DIR)
		set(AQ_SPV_OUT_DIR "${AQ_SPV_SHADER_DIR}/spv")
	endif()
	if(NOT AQ_SPV_ARGS_FILE)
		set(AQ_SPV_ARGS_FILE "${AQ_SPV_LIST_DIR}/dxc_args.txt")
	endif()
	if(NOT DEFINED AQ_SPV_DEBUG_INFO)
		set(AQ_SPV_DEBUG_INFO OFF)
	endif()
endmacro()


# ----------------------------------------------------------------------------
#  全エントリをコンパイルする (スクリプト実行の本体)
# ----------------------------------------------------------------------------
function(aq_spv_compile_all)
	aq_spv_resolve_defaults()
	aq_spv_resolve_dxc(dxc)
	aq_spv_read_args("${AQ_SPV_ARGS_FILE}" "${AQ_SPV_DEBUG_INFO}" commonArgs shaderModel)
	aq_spv_read_entries("${AQ_SPV_ENTRIES}" entries)

	file(MAKE_DIRECTORY "${AQ_SPV_OUT_DIR}")

	list(LENGTH entries entryCount)
	message(STATUS "[compile_spv] dxc      = ${dxc}")
	message(STATUS "[compile_spv] shaders  = ${AQ_SPV_SHADER_DIR}")
	message(STATUS "[compile_spv] out      = ${AQ_SPV_OUT_DIR}")
	message(STATUS "[compile_spv] entries  = ${entryCount}")

	set(failed 0)
	foreach(record IN LISTS entries)
		string(REPLACE "|" ";" parts "${record}")
		list(GET parts 0 fx)
		list(GET parts 1 entry)
		list(GET parts 2 stage)

		get_filename_component(stem "${fx}" NAME_WE)
		set(src "${AQ_SPV_SHADER_DIR}/${fx}")
		set(out "${AQ_SPV_OUT_DIR}/${stem}.${entry}.${stage}.spv")

		if(NOT EXISTS "${src}")
			message(SEND_ERROR "[compile_spv] .fx がありません: ${src}")
			math(EXPR failed "${failed}+1")
			continue()
		endif()

		execute_process(
			COMMAND "${dxc}"
			        ${commonArgs}
			        -E "${entry}"
			        -T "${stage}_${shaderModel}"
			        -I "${AQ_SPV_SHADER_DIR}"
			        -Fo "${out}"
			        "${src}"
			RESULT_VARIABLE rc
			OUTPUT_VARIABLE stdOut
			ERROR_VARIABLE  stdErr
		)
		if(NOT rc EQUAL 0)
			message(SEND_ERROR "[compile_spv] 失敗: ${fx} ${entry} (${stage})\n${stdOut}${stdErr}")
			math(EXPR failed "${failed}+1")
		elseif(NOT stdErr STREQUAL "")
			message(STATUS "[compile_spv] 警告: ${fx} ${entry} (${stage})\n${stdErr}")
		endif()
	endforeach()

	if(NOT failed EQUAL 0)
		message(FATAL_ERROR "[compile_spv] ${failed} 件のシェーダがコンパイルできませんでした")
	endif()
	message(STATUS "[compile_spv] 完了 (${entryCount} 本)")
endfunction()


# ----------------------------------------------------------------------------
#  include されたとき用: 生成ターゲットを 1 つ足す
#
#  ビルドのたびに全エントリを再生成する (エントリ数 59 本・数秒)。
#  .fx は Common.fx / Lighting.fx などを #include するため、
#  ソース単体のタイムスタンプ比較では依存が拾えず、部分再生成は正しくない。
# ----------------------------------------------------------------------------
function(aq_add_compile_spv_target targetName)
	aq_spv_resolve_defaults()

	set(scriptArgs
		-D "AQ_SPV_SHADER_DIR=${AQ_SPV_SHADER_DIR}"
		-D "AQ_SPV_ENTRIES=${AQ_SPV_ENTRIES}"
		-D "AQ_SPV_OUT_DIR=${AQ_SPV_OUT_DIR}"
		-D "AQ_SPV_ARGS_FILE=${AQ_SPV_ARGS_FILE}"
		-D "AQ_SPV_DEBUG_INFO=${AQ_SPV_DEBUG_INFO}"
	)
	if(AQ_SPV_DXC)
		list(APPEND scriptArgs -D "AQ_SPV_DXC=${AQ_SPV_DXC}")
	endif()

	add_custom_target(${targetName}
		COMMAND ${CMAKE_COMMAND} ${scriptArgs} -P "${AQ_SPV_LIST_DIR}/compile_spv.cmake"
		COMMENT "SPIR-V を生成中 (${AQ_SPV_ENTRIES})"
		VERBATIM
	)
endfunction()


# cmake -P で直接叩かれたときだけ実行する (include 時は関数定義だけ)
if(CMAKE_SCRIPT_MODE_FILE)
	get_filename_component(aqSpvThisFile "${CMAKE_CURRENT_LIST_FILE}" ABSOLUTE)
	get_filename_component(aqSpvScriptFile "${CMAKE_SCRIPT_MODE_FILE}" ABSOLUTE)
	if(aqSpvThisFile STREQUAL aqSpvScriptFile)
		aq_spv_compile_all()
	endif()
endif()
