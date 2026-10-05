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

    def use_session_snapshot(self):
        self.report["schemaVersion"] = 4
        self.report["settings"].update(emulationSettingsSha256="emulation-hash", gameProfileSha256="profile-hash", source="session-start")
        self.report["graphics"] = {"identitySource": "session-start", "vulkanReported": {"pipelineCacheUUID": "driver-a", "driverVersion": "1"}}

    def test_driver_change_only_does_not_invalidate_normalized_configuration(self):
        self.use_session_snapshot()
        report = copy.deepcopy(self.report)
        report["settings"]["settingsSha256"] = "raw-hash-changed-by-driver-path"
        report["graphics"]["vulkanReported"] = {"pipelineCacheUUID": "driver-b", "driverVersion": "2"}
        self.assertTrue(self.comparison(report)["comparable"])

    def test_profile_or_normalized_settings_change_is_rejected(self):
        self.use_session_snapshot()
        for key in ("emulationSettingsSha256", "gameProfileSha256"):
            report = copy.deepcopy(self.report)
            report["settings"][key] = "changed"
            self.assertFalse(self.comparison(report)["comparable"])

    def test_current_or_missing_snapshot_cannot_replace_session_configuration(self):
        self.use_session_snapshot()
        for source in ("export-time", "unavailable"):
            report = copy.deepcopy(self.report)
            report["settings"]["source"] = source
            self.assertFalse(self.comparison(report)["comparable"])
        report = copy.deepcopy(self.report)
        report["graphics"]["vulkanReported"] = {}
        self.assertFalse(self.comparison(report)["comparable"])


if __name__ == "__main__":
    unittest.main()
