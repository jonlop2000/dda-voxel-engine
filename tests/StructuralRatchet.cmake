# keep source files within their recorded line limits.
#
# fails if any file listed in tests/app-size-ceiling.txt has more lines
# than its committed ceiling. the ceiling may only be lowered (see that file).
#
# run standalone:
#   cmake -DSOURCE_DIR=<repo-root> -P tests/StructuralRatchet.cmake
# registered as the ctest case `structural_app_size_ratchet`, so it runs in ci via
# `ctest` with no dependency on building the engine.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required (pass -DSOURCE_DIR=<repo-root>)")
endif()

set(ceiling_file "${SOURCE_DIR}/tests/app-size-ceiling.txt")
if(NOT EXISTS "${ceiling_file}")
    message(FATAL_ERROR "Ratchet ceiling file missing: ${ceiling_file}")
endif()

# canonical line count = number of '\n' characters (matches `wc -l`), computed by
# length difference so embedded ';' in source never gets treated as a cmake list
# separator.
function(count_newlines path out_var)
    file(READ "${path}" content)
    string(LENGTH "${content}" with_nl)
    string(REPLACE "\n" "" stripped "${content}")
    string(LENGTH "${stripped}" without_nl)
    math(EXPR n "${with_nl} - ${without_nl}")
    set(${out_var} "${n}" PARENT_SCOPE)
endfunction()

file(STRINGS "${ceiling_file}" raw_lines)
set(violations "")
set(checked 0)

foreach(line IN LISTS raw_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    if(NOT line MATCHES "^(.+)=(.+)$")
        message(FATAL_ERROR "Malformed ceiling line (expected 'path = count'): '${line}'")
    endif()
    string(STRIP "${CMAKE_MATCH_1}" rel_path)
    string(STRIP "${CMAKE_MATCH_2}" ceiling)

    set(abs_path "${SOURCE_DIR}/${rel_path}")
    if(NOT EXISTS "${abs_path}")
        message(FATAL_ERROR "Ratchet-tracked file is missing: ${abs_path}")
    endif()

    count_newlines("${abs_path}" actual)
    math(EXPR checked "${checked} + 1")

    if(actual GREATER ceiling)
        math(EXPR over "${actual} - ${ceiling}")
        list(APPEND violations "  ${rel_path}: ${actual} lines > ceiling ${ceiling} (+${over})")
    else()
        math(EXPR slack "${ceiling} - ${actual}")
        if(slack GREATER 0)
            message(STATUS "[ratchet] ${rel_path}: ${actual} <= ${ceiling} (ceiling ${slack} above current — lower it via scripts/refactor-metrics)")
        else()
            message(STATUS "[ratchet] ${rel_path}: ${actual} == ${ceiling} (at ceiling)")
        endif()
    endif()
endforeach()

if(checked EQUAL 0)
    message(FATAL_ERROR "Ratchet ceiling file has no entries: ${ceiling_file}")
endif()

if(NOT violations STREQUAL "")
    string(REPLACE ";" "\n" violation_text "${violations}")
    message(FATAL_ERROR
        "\nAnti-growth ratchet FAILED — a tracked file grew past its committed ceiling:\n"
        "${violation_text}\n\n"
        "Keep new implementation in focused modules.\n"
        "Put new code in a module under src/engine/... (or an existing module), not in App.\n"
        "If this growth is genuinely intended, RAISE the ceiling in tests/app-size-ceiling.txt\n"
        "in this same PR so the decision is explicit and reviewed.\n")
endif()

message(STATUS "[ratchet] OK — ${checked} tracked file(s) within ceiling.")
