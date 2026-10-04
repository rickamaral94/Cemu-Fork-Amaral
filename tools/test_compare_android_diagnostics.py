import copy
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from compare_android_diagnostics import compare


class DiagnosticComparisonTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.report = {
            "app": {"commit": "same-commit"},
            "session": {"titleId": "0005000010143500"},
            "settings": {"settingsSha256": "settings-hash", "activeGraphicPacksSha256": "packs-hash"},
        }
        self.windows = [{"renderCpuMs": 40.0, "gpuTimeMs": None, "queueSubmitCalls": 40, "pipelineCreations": 2}]

    def bundle(self, name, report, windows):
        path = Path(self.directory.name, name)
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("report.json", json.dumps(report))
            archive.writestr("performance-windows-v1.jsonl", "\n".join(json.dumps(window) for window in windows))
        return path

    def comparison(self, second_report=None, second_windows=None):
        return compare(
            self.bundle("a.zip", self.report, self.windows),
            self.bundle("b.zip", self.report if second_report is None else second_report,
                        self.windows if second_windows is None else second_windows),
        )

    def test_missing_gpu_remains_unavailable(self):
        result = self.comparison()
        self.assertTrue(result["comparable"])
        self.assertIsNone(result["metrics"]["gpuTimeMs"]["a"])
        self.assertIsNone(result["metrics"]["gpuTimeMs"]["b"])

    def test_different_configuration_is_rejected(self):
        report = copy.deepcopy(self.report)
        report["settings"]["settingsSha256"] = "different"
        self.assertFalse(self.comparison(report)["comparable"])

    def test_unknown_identity_is_rejected_even_when_equal(self):
        self.report["session"]["titleId"] = "unavailable"
        self.assertFalse(self.comparison()["comparable"])

    def test_missing_identity_and_empty_windows_are_rejected(self):
        self.assertFalse(self.comparison({})["comparable"])
        self.assertFalse(self.comparison(second_windows=[])["comparable"])

    def test_totals_and_cpu_delta_cover_all_windows(self):
        result = self.comparison(second_windows=[
            {"renderCpuMs": 30.0, "queueSubmitCalls": 20},
            {"renderCpuMs": 32.0, "queueSubmitCalls": 25},
        ])
        self.assertEqual(-9.0, result["metrics"]["renderCpuMs"]["medianDelta"])
        self.assertEqual({"a": 40, "b": 45}, result["counts"]["submits"])


if __name__ == "__main__":
    unittest.main()
