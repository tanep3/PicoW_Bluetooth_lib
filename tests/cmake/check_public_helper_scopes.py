#!/usr/bin/env python3
"""Configure-only CMake helper scope tests. No real SDK, patcher, or firmware build."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[2]
NORMAL_HELPERS = [
    "a2dp_source", "a2dp_sink", "spp", "avrcp_target", "avrcp_controller",
    "avrcp_browsing", "avrcp_cover_art",
]
EXPERIMENT_HELPERS = ["a2dp_same_peer_experiment"] + [
    helper for helper in NORMAL_HELPERS if helper != "a2dp_sink"
]
ROOTS = [
    "PICOW_A2DP_ROOT", "PICOW_A2DP_SINK_ROOT", "PICOW_SPP_ROOT",
    "PICOW_AVRCP_ROOT", "PICOW_AVRCP_OVERLAY_ROOT", "PICOW_SPP_OVERLAY_ROOT",
    "PICOW_A2DP_EXPERIMENT_ROOT",
    "PICOW_SINK_RAW_ROOT",
]
OVERLAYS = {
    "avrcp": ["avrcp.c", "avrcp_target.c", "avrcp_controller.c"],
    "spp": ["l2cap.c", "rfcomm.c"],
    "browsing": ["avrcp_browsing.c", "avrcp_browsing_controller.c"],
    "cover": ["avrcp_cover_art_client.c", "goep_client.c"],
    "experiment": ["a2dp.c", "avdtp.c", "avdtp_acceptor.c",
                   "avdtp_initiator.c", "avdtp_sink.c"],
    "sink": ["avdtp_acceptor.c"],
}
PATCHERS = {
    "avrcp": ["patch_avrcp_common", "patch_avrcp_target", "patch_avrcp_controller"],
    "spp": ["patch_spp_ertm_sdk", "patch_spp_ertm_sdk"],
    "browsing": ["patch_avrcp_browsing", "patch_avrcp_browsing_controller"],
    "cover": ["patch_avrcp_cover_art", "patch_goep_client"],
    "experiment": ["patch_a2dp_same_peer"] * 5,
    "sink": ["patch_sink_raw_configuration"],
}
SDK_SOURCES = sorted(set(sum(OVERLAYS.values(), [])))
SOURCES = {
    "a2dp_source": ["bluetooth-runtime/BluetoothRuntime.cpp",
                    "audio-core/A2dpSourcePlayer.cpp", "audio-core/AudioPipeline.cpp",
                    "audio-core/BTStackA2DPTransport.cpp",
                    "audio-core/encoders/SBCEncoder_BTStack.cpp"],
    "a2dp_sink": ["bluetooth-runtime/BluetoothRuntime.cpp", "audio-core/A2dpSinkReceiver.cpp"],
    "spp": ["bluetooth-runtime/BluetoothRuntime.cpp", "spp-sdk/SppClient.cpp",
            "spp-sdk/SdkPortConfiguration.c"],
    "avrcp_target": ["bluetooth-runtime/BluetoothRuntime.cpp", "avrcp-sdk/AvrcpTarget.cpp"],
}
SOURCES["avrcp_controller"] = SOURCES["avrcp_target"] + ["avrcp-sdk/AvrcpController.cpp"]
SOURCES["avrcp_browsing"] = SOURCES["avrcp_controller"] + ["avrcp-sdk/AvrcpBrowsingController.cpp"]
SOURCES["avrcp_cover_art"] = SOURCES["avrcp_controller"] + ["avrcp-sdk/AvrcpCoverArt.cpp"]
SOURCES["a2dp_same_peer_experiment"] = []

# Stand-in patch scripts test only path lookup, invocation, and CMake selection.
# Never copy or execute the production patchers, or claim SDK transformations passed.
PATCHER = '''from pathlib import Path
import sys
script = Path(__file__).resolve()
original, output = map(Path, sys.argv[-2:])
assert original.is_file(), original
source = original.resolve().relative_to(script.parents[2] / "sdk/lib/btstack").as_posix()
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("/* synthetic overlay: " + script.name + " <- " + source + " */\\n")
with (script.parent.parent / "patch_calls.txt").open("a") as log:
    log.write(script.name + ":" + source + ":" + output.name + "\\n")
'''


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def includes(helpers):
    return "\n".join('include("${FIXTURE_REPO}/cmake/picow_%s.cmake")' % h for h in helpers) + "\n"


def consumers(label, helpers):
    lines = []
    for helper in helpers:
        target = label + "_" + helper
        lines.append('add_executable(%s "${CMAKE_SOURCE_DIR}/empty.cpp")' % target)
        lines.append('target_link_libraries(%s PRIVATE pico_btstack_classic)' % target)
        if helper == "a2dp_same_peer_experiment":
            call = "picow_enable_a2dp_same_peer_experiment(%s)" % target
        else:
            call = 'picow_target_%s(%s "${CMAKE_CURRENT_SOURCE_DIR}/config")' % (helper, target)
        # Repeated calls on the same target must remain valid and not rerun overlays.
        lines.extend([call, call])
        lines.append("get_target_property(actual %s SOURCES)" % target)
        for source in SOURCES[helper]:
            lines.extend([
                'list(FIND actual "${FIXTURE_REPO}/%s" found)' % source,
                'if(found EQUAL -1)',
                '  message(FATAL_ERROR "SCOPE_SOURCE %s missing %s: ${actual}")' % (target, source),
                'endif()',
            ])
        if helper != "a2dp_same_peer_experiment":
            lines.extend([
                "get_target_property(includes %s INCLUDE_DIRECTORIES)" % target,
                'list(FIND includes "${FIXTURE_REPO}/bluetooth-runtime" found)',
                'if(found EQUAL -1)',
                '  message(FATAL_ERROR "SCOPE_INCLUDE %s: ${includes}")' % target,
                'endif()',
                'list(FIND includes "${CMAKE_CURRENT_SOURCE_DIR}/config" found)',
                'if(found EQUAL -1)',
                '  message(FATAL_ERROR "Consumer config directory lost: %s")' % target,
                'endif()',
            ])
    lines.append('add_executable(%s_stock "${CMAKE_SOURCE_DIR}/empty.cpp")' % label)
    lines.append('target_link_libraries(%s_stock PRIVATE pico_btstack_classic)' % label)
    return "\n".join(lines) + "\n"


def make_fixture(work, helper_dir, scenario, helpers, mutation=None):
    root, sdk, src = work / "repo", work / "sdk", work / "project"
    shutil.copytree(helper_dir, root / "cmake")
    if mutation:
        filename, line, *replacement = mutation
        path = root / "cmake" / filename
        text = path.read_text()
        if text.count(line) != 1:
            raise AssertionError("Mutation did not identify one statement: " + filename)
        path.write_text(text.replace(line, replacement[0] if replacement else "", 1))
    for source in set(sum(SOURCES.values(), [])):
        write(root / source, "/* synthetic library source */\n")
    tools = sorted(set(sum(PATCHERS.values(), [])))
    for name in tools:
        write(root / "tools" / (name + ".py"), PATCHER)
    for name in ["avrcp_controller_fragments", "avrcp_controller_custom",
                 "avrcp_target_metadata_patch", "a2dp_media_cid_patch",
                 "a2dp_role_lifecycle_patch", "a2dp_transaction_patch"]:
        write(root / "tools" / (name + ".py"), "# synthetic configure dependency\n")
    btstack = sdk / "lib/btstack"
    for name in SDK_SOURCES + ["btstack_link_key_db_memory.c"]:
        relative = "src/" + ("" if name == "l2cap.c" else "classic/") + name
        write(btstack / relative, "/* synthetic SDK source */\n")
    # Only the sink helper's required text markers, not a real/usable linker map.
    write(sdk / "src/rp2_common/pico_crt0/rp2040/memmap_default.ld",
          "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 256k\n"
          "KEEP(*(.stack*))\n    } > SCRATCH_Y\n"
          "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);\n")
    write(src / "empty.cpp", "int main() { return 0; }\n")
    preamble = '''cmake_minimum_required(VERSION 3.13)
project(public_helper_scopes C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BTSTACK_PATH "${PICO_SDK_PATH}/lib/btstack")
foreach(lib pico_stdlib pico_cyw43_arch_poll pico_btstack_base pico_btstack_classic
            pico_btstack_cyw43 pico_btstack_sbc_encoder pico_btstack_sbc_decoder)
  add_library(${lib} INTERFACE)
endforeach()
target_sources(pico_btstack_base INTERFACE "${PICO_BTSTACK_PATH}/src/l2cap.c")
target_link_libraries(pico_btstack_classic INTERFACE pico_btstack_base)
function(pico_set_linker_script target script)
  if(NOT EXISTS "${script}")
    message(FATAL_ERROR "Synthetic sink linker script missing")
  endif()
endfunction()
'''
    for name in SDK_SOURCES:
        if name != "l2cap.c":
            preamble += 'target_sources(pico_btstack_classic INTERFACE "${PICO_BTSTACK_PATH}/src/classic/%s")\n' % name
    calls = consumers("first", helpers)
    inc = includes(helpers)
    labels = ["first"]
    if scenario == "same":
        body = inc + inc + calls + consumers("second", helpers)
        labels += ["second"]
    elif scenario == "siblings":
        body = "add_subdirectory(first)\nadd_subdirectory(second)\n"
        write(src / "first/CMakeLists.txt", inc + inc + calls)
        write(src / "second/CMakeLists.txt", inc + inc + consumers("second", helpers))
        labels += ["second"]
    elif scenario == "include_only_sibling":
        body = "add_subdirectory(loader)\nadd_subdirectory(first)\n"
        write(src / "loader/CMakeLists.txt", inc)
        write(src / "first/CMakeLists.txt", inc + calls)
    elif scenario == "function_include":
        body = "function(load_helpers)\n" + inc + "endfunction()\nload_helpers()\n" + inc + calls
    elif scenario == "function_include_sibling":
        body = "add_subdirectory(loader)\nadd_subdirectory(first)\n"
        write(src / "loader/CMakeLists.txt",
              "function(load_helpers)\n" + inc + "endfunction()\nload_helpers()\n")
        write(src / "first/CMakeLists.txt", inc + calls)
    elif scenario == "parent_include":
        body = inc + "add_subdirectory(first)\n"
        write(src / "first/CMakeLists.txt", inc + calls)
    elif scenario == "shadowed_roots":
        body = inc + "\n".join('set(%s "${CMAKE_SOURCE_DIR}/wrong-root")' % root for root in ROOTS) + "\n" + calls
    elif scenario == "composition":
        body = "add_subdirectory(loader)\nadd_subdirectory(first)\n"
        write(src / "loader/CMakeLists.txt",
              'include("${FIXTURE_REPO}/cmake/picow_avrcp_target.cmake")\n')
        write(src / "first/CMakeLists.txt", inc + calls)
    else:
        raise AssertionError(scenario)
    write(src / "CMakeLists.txt", preamble + body)
    return root, sdk, src, labels


def enabled_groups(helper):
    enabled = set()
    if helper.startswith("avrcp_"):
        enabled.add("avrcp")
    if helper in ["spp", "avrcp_browsing", "avrcp_cover_art"]:
        enabled.add("spp")
    if helper == "avrcp_browsing":
        enabled.add("browsing")
    if helper == "avrcp_cover_art":
        enabled.add("cover")
    if helper == "a2dp_same_peer_experiment":
        enabled.add("experiment")
    if helper == "a2dp_sink":
        enabled.add("sink")
    return enabled


def verify(work, root, labels, helpers):
    rows = json.loads((work / "build/compile_commands.json").read_text())
    sdk = work / "sdk/lib/btstack"
    for label in labels:
        for helper in helpers + ["stock"]:
            target = label + "_" + helper
            selected = [Path(row["file"]) for row in rows if "CMakeFiles/%s.dir/" % target in row["command"]]
            enabled = enabled_groups(helper)
            for name in SDK_SOURCES:
                matches = [path for path in selected if path.name == name]
                if len(matches) != 1:
                    raise AssertionError((target, name, matches))
                original = sdk / "src" / ("" if name == "l2cap.c" else "classic") / name
                owners = [group for group in enabled if name in OVERLAYS[group]]
                if len(owners) > 1:
                    raise AssertionError(("conflicting fixture owners", target, name, owners))
                if not owners:
                    if matches[0] != original:
                        raise AssertionError((target, name, "original source replaced", matches[0]))
                else:
                    group = owners[0]
                    patcher = PATCHERS[group][OVERLAYS[group].index(name)] + ".py"
                    expected = ("/* synthetic overlay: " + patcher + " <- "
                                + original.relative_to(sdk).as_posix() + " */\n")
                    if matches[0] == original or matches[0].read_text() != expected:
                        raise AssertionError((target, name, "wrong overlay selected", matches[0], patcher))
    calls = (root / "patch_calls.txt").read_text().splitlines()
    groups = set().union(*(enabled_groups(helper) for helper in helpers))
    expected = sorted(patcher + ".py:src/" + ("" if name == "l2cap.c" else "classic/")
                      + name + ":" + name for group in groups
                      for patcher, name in zip(PATCHERS[group], OVERLAYS[group]))
    if sorted(calls) != expected:
        raise AssertionError(("Repeated preparation or missing patch invocation", calls))


def run(work, helper_dir, scenario, helpers, mutation=None, error=None, verification_error=None):
    if error and verification_error:
        raise ValueError("Specify only one expected failure stage")
    root, sdk, src, labels = make_fixture(work, helper_dir, scenario, helpers, mutation)
    build = work / "build"
    tmp = work / "tmp"
    tmp.mkdir()
    env = dict(os.environ, TMPDIR=str(tmp))
    command = ["cmake", "-G", "Unix Makefiles", "-S", str(src), "-B", str(build),
               "-DFIXTURE_REPO=" + str(root), "-DPICO_SDK_PATH=" + str(sdk)]
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    write(work / "configure.log", result.stdout)
    if error:
        if result.returncode == 0 or error not in result.stdout:
            raise AssertionError("Expected failure %r not seen: %s" % (error, work / "configure.log"))
    elif result.returncode:
        raise AssertionError("Configure failed: " + str(work / "configure.log") + "\n" + result.stdout)
    else:
        try:
            verify(work, root, labels, helpers)
        except AssertionError as exc:
            if not verification_error or verification_error not in str(exc):
                raise
        else:
            if verification_error:
                raise AssertionError("Expected verification failure not seen: " + verification_error)
    print("PASS " + work.name, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper-dir", type=Path, default=REPO / "cmake")
    parser.add_argument("--work-parent", type=Path, default=REPO / ".work")
    args = parser.parse_args()
    args.work_parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="public-helper-scopes-", dir=str(args.work_parent.resolve())))
    print("Fixture/log directory: " + str(work), flush=True)
    scenarios = ["same", "siblings", "include_only_sibling", "function_include",
                 "function_include_sibling", "parent_include", "shadowed_roots", "composition"]
    # Normal Sink and the same-peer experiment both select avdtp_acceptor.c.
    # Production deliberately rejects mixing them in one CMake configuration.
    # Test each supported scope separately rather than weakening that guard.
    for family, helpers in [("normal", NORMAL_HELPERS), ("experiment", EXPERIMENT_HELPERS)]:
        for scenario in scenarios:
            run(work / (family + "_" + scenario), args.helper_dir, scenario, helpers)
    # Keep the previous experiment-last composition too. Both orders must leave
    # Source/SPP/AVRCP and unmodified siblings on their own selected SDK sources.
    for scenario in ["same", "siblings"]:
        run(work / ("experiment_last_" + scenario), args.helper_dir, scenario,
            EXPERIMENT_HELPERS[1:] + EXPERIMENT_HELPERS[:1])
    # Deliberately remove individual fixes in disposable copies. A passing
    # positive suite alone must not conceal preparation already done by a sibling.
    negatives = [
        ("source", "picow_a2dp_source.cmake", "PICOW_A2DP_ROOT", "PICOW_A2DP_ROOT", "siblings", "SCOPE_SOURCE", NORMAL_HELPERS),
        ("controller", "picow_avrcp_controller.cmake", "PICOW_AVRCP_ROOT", "PICOW_AVRCP_ROOT", "composition", "SCOPE_SOURCE", NORMAL_HELPERS),
        ("overlay", "picow_avrcp_sdk_overlay.cmake", "PICOW_AVRCP_OVERLAY_ROOT", "PICOW_AVRCP_OVERLAY_ROOT", "function_include", "AVRCP common overlay generation failed", NORMAL_HELPERS),
        ("sink", "picow_a2dp_sink.cmake", "PICOW_A2DP_SINK_ROOT", "PICOW_A2DP_SINK_ROOT", "siblings", "SCOPE_SOURCE", NORMAL_HELPERS),
        ("sink_overlay", "picow_sink_raw_configuration.cmake", "root", "PICOW_SINK_RAW_ROOT", "function_include", "Sink raw guard generation failed", NORMAL_HELPERS),
        ("experiment", "picow_a2dp_same_peer_experiment.cmake", "PICOW_A2DP_EXPERIMENT_ROOT", "PICOW_A2DP_EXPERIMENT_ROOT", "function_include", "Experimental A2DP overlay failed", EXPERIMENT_HELPERS),
    ]
    for label, filename, variable, prop, scenario, error, helpers in negatives:
        # Match the statement without indentation so both existing styles work.
        statement = "get_property(%s GLOBAL PROPERTY %s)" % (variable, prop)
        run(work / ("negative_" + label), args.helper_dir, scenario, helpers,
            mutation=(filename, statement), error=error)
    # Fail-closed source-selection conflicts, tested in both preparation orders.
    run(work / "conflict_sink_first", args.helper_dir, "same",
        ["a2dp_sink", "a2dp_same_peer_experiment"], error="SDK A2DP source layout changed")
    run(work / "conflict_experiment_first", args.helper_dir, "same",
        ["a2dp_same_peer_experiment", "a2dp_sink"],
        error="Sink raw guard requires direct SDK avdtp_acceptor.c")
    # Same generator handles multiple source files. Deliberately misroute an
    # existing input while leaving output names/source selection intact; the
    # stand-in output marker must catch this even when configuration succeeds.
    for label, filename, helpers, source in [
        ("experiment", "picow_a2dp_same_peer_experiment.cmake", ["a2dp_same_peer_experiment"], "src/classic/a2dp.c"),
        ("spp", "picow_spp_sdk_overlay.cmake", ["spp"], "src/l2cap.c"),
    ]:
        run(work / ("wrong_input_" + label), args.helper_dir, "same", helpers,
            mutation=(filename, ' "${original}" "${output}"',
                      ' "${PICO_BTSTACK_PATH}/' + source + '" "${output}"'),
            verification_error="wrong overlay selected")
    print("PASS 18 positive and 10 expected-failure configure-only cases; no firmware built")


if __name__ == "__main__":
    main()
