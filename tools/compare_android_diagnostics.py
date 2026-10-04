#!/usr/bin/env python3
"""Compare two Cemu Fork Amaral diagnostic ZIPs without extracting game data."""

import argparse
import json
import statistics
import zipfile


IDENTITY_PATHS = (
    ("app", "commit"),
    ("session", "titleId"),
    ("settings", "settingsSha256"),
    ("settings", "activeGraphicPacksSha256"),
)


def read_bundle(path):
    with zipfile.ZipFile(path) as bundle:
        report = json.loads(bundle.read("report.json"))
        windows = []
        if "performance-windows-v1.jsonl" in bundle.namelist():
            windows = [json.loads(line) for line in bundle.read("performance-windows-v1.jsonl").decode().splitlines() if line]
    return report, windows


def value_at(data, path):
    for part in path:
        if not isinstance(data, dict):
            return None
        data = data.get(part)
    return data


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    return values[max(0, min(len(values) - 1, int(len(values) * fraction + 0.999999) - 1))]


def metric(windows, name):
    values = [window[name] for window in windows if window.get(name) is not None]
    return {"median": statistics.median(values), "p95": percentile(values, 0.95)} if values else None


def compare(first_path, second_path):
    first_report, first_windows = read_bundle(first_path)
    second_report, second_windows = read_bundle(second_path)
    divergences = []
    for path in IDENTITY_PATHS:
        left, right = value_at(first_report, path), value_at(second_report, path)
        if left != right or left is None or right is None or str(left).lower().startswith("unavailable") or str(right).lower().startswith("unavailable"):
            divergences.append({"field": ".".join(path), "a": left, "b": right})
    metrics = {}
    for name in ("frameMsMedian", "renderCpuMs", "gpuTimeMs", "gpuTimePerFrameMs", "gpuFrameMsP95", "gpuTimingCpuMs", "fenceWaitMs", "queueSubmitCpuMs"):
        left, right = metric(first_windows, name), metric(second_windows, name)
        metrics[name] = {"a": left, "b": right}
        if left and right:
            metrics[name]["medianDelta"] = right["median"] - left["median"]
            metrics[name]["p95Delta"] = right["p95"] - left["p95"]
    return {
        "schemaVersion": 1,
        "comparable": not divergences and bool(first_windows) and bool(second_windows),
        "configurationDivergences": divergences,
        "thermal": {"a": first_report.get("device", {}).get("thermalStatus"), "b": second_report.get("device", {}).get("thermalStatus")},
        "gpuCoveragePct": {"a": first_report.get("performance", {}).get("gpuCoveragePct"), "b": second_report.get("performance", {}).get("gpuCoveragePct")},
        "metrics": metrics,
        "counts": {
            "pipelineCreations": {"a": sum(w.get("pipelineCreations", 0) for w in first_windows), "b": sum(w.get("pipelineCreations", 0) for w in second_windows)},
            "submits": {"a": sum(w.get("queueSubmitCalls", 0) for w in first_windows), "b": sum(w.get("queueSubmitCalls", 0) for w in second_windows)},
            "framesOver33_3Ms": {"a": sum(w.get("framesOver33_3Ms", 0) or 0 for w in first_windows), "b": sum(w.get("framesOver33_3Ms", 0) or 0 for w in second_windows)},
            "swapchainRecreates": {"a": sum(w.get("swapchainRecreates", 0) for w in first_windows), "b": sum(w.get("swapchainRecreates", 0) for w in second_windows)},
        },
        "note": "Driver causality requires comparable identity, scene, thermal state and metric coverage; FPS alone is insufficient.",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("session_a")
    parser.add_argument("session_b")
    args = parser.parse_args()
    print(json.dumps(compare(args.session_a, args.session_b), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
