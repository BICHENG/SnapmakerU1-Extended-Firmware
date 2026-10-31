#!/usr/bin/env python3
"""Validate capture framing and descriptor bounds; never dereference pointer values."""
import argparse
import collections
import json
import pathlib
import struct

GUI_SHA256 = "2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472"
TASK_NAMES = dict(enumerate(("none", "fill", "border", "box_shadow", "label", "image", "layer", "line", "arc", "triangle", "mask_rectangle", "mask_bitmap", "vector")))


def check(directory):
    report = json.loads((directory / "report.json").read_text())
    for field in ("complete", "binary_verified", "callback_restored", "event_observer_removed"):
        if report.get(field) is not True:
            raise ValueError(f"{field} is not true: {report.get('reason')}")
    for field in ("drawing_replaced", "input_sent", "descriptor_pointers_replayable"):
        if report.get(field) is not False:
            raise ValueError(f"unexpected {field}")
    if (report.get("vendor_sha256"), report.get("vendor_bytes")) != (GUI_SHA256, 6563736):
        raise ValueError("unsupported GUI identity")
    if tuple(report.get(k) for k in ("width", "height", "stride", "color_format")) != (480, 320, 1920, 17):
        raise ValueError("unsupported frame layout")
    for name in ("initial.bgra", "final.bgra"):
        if (directory / name).stat().st_size != 614400:
            raise ValueError(f"wrong frame size: {name}")
    descriptors = (directory / "descriptors.bin").read_bytes()
    tasks = [json.loads(line) for line in (directory / "tasks.jsonl").read_text().splitlines()]
    if not 0 < len(tasks) <= 2048 or len(tasks) != report.get("task_count"):
        raise ValueError("wrong task count")
    if len(descriptors) != report.get("descriptor_bytes") or len(descriptors) > 1024 * 1024:
        raise ValueError("wrong descriptor total")
    offset = 0
    counts = collections.Counter()
    for sequence, task in enumerate(tasks):
        kind, size = task["type"], task["dsc_size"]
        if not 1 <= kind <= 12 or not 48 <= size <= 1024 or size % 8:
            raise ValueError("unknown task or descriptor ABI")
        if kind in (1, 4) and size != {1: 112, 4: 128}[kind]:
            raise ValueError("known descriptor size mismatch")
        if task["sequence"] != sequence or task["descriptor_offset"] != offset or offset + size > len(descriptors):
            raise ValueError("non-contiguous descriptor snapshot")
        raw_layer, raw_size = struct.unpack_from("<QQ", descriptors, offset + 24)
        if raw_layer != int(task["layer"], 16) or raw_size != size:
            raise ValueError("descriptor prefix disagrees with metadata")
        for key in ("area", "real_area", "clip", "original_clip", "buffer_area"):
            if not isinstance(task[key], list) or len(task[key]) != 4 or any(type(v) is not int for v in task[key]):
                raise ValueError(f"invalid {key}")
        if task["elapsed_ns"] < 0:
            raise ValueError("negative task timestamp")
        offset += size
        counts[TASK_NAMES[kind]] += 1
    if offset != len(descriptors):
        raise ValueError("unreferenced descriptor bytes")
    start, end = report["render_start_ns"], report["last_flush_finish_ns"]
    if not 0 < start <= end or report["flushes"] < 1:
        raise ValueError("invalid frame event boundaries")
    for key in ("initial_copy_ns", "final_copy_ns", "task_observer_ns"):
        if report[key] < 0:
            raise ValueError(f"negative {key}")
    return {"passed": True, "task_counts": dict(sorted(counts.items())), "descriptor_bytes": offset,
            "render_to_last_flush_ns": end - start,
            "initial_copy_ns": report["initial_copy_ns"], "final_copy_ns": report["final_copy_ns"],
            "task_observer_ns": report["task_observer_ns"],
            "interpretation": "Observed frame interval includes capture overhead; it is not animation FPS or GPU speedup.",
            "descriptor_pointers_replayable": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.directory), indent=2))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f"capture rejected: {error}\n")
