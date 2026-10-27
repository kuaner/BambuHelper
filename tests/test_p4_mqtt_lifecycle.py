import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4MqttLifecycleTests(unittest.TestCase):
    def test_stop_and_resume_are_ordered_per_slot(self):
        output = ROOT / ".cache" / "p4-mqtt-lifecycle-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-pthread", "-DP4_MQTT_LIFECYCLE_TEST_HOOK", "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "p4_mqtt_lifecycle_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
