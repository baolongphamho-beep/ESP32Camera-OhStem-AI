from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from power_monitor import format_telemetry_line, telemetry_change_key


class PowerMonitorTests(unittest.TestCase):
    def test_telemetry_change_key_dedups_active_jitter_and_tracks_result_hold(self):
        active_a = {
            "phase": "ACTIVE",
            "cpu_mhz": 240,
            "wifi_ps": "NONE",
            "wifi_started": True,
            "light_sleep_enabled": True,
            "usb_sleep_blocked": True,
            "no_ls_locks": 2,
            "ble_duty_percent": 12.5,
            "weight_stable": False,
            "lcd_backlight": True,
            "hold_sec": 10,
            "height_cm": 170.1,
            "bmi": 22.10,
        }
        active_b = dict(active_a, height_cm=170.4, bmi=22.15)
        self.assertEqual(telemetry_change_key(active_a), telemetry_change_key(active_b))

        hold_a = dict(active_a, phase="RESULT_HOLD", cpu_mhz=80, ble_duty_percent=10.0,
                      weight_stable=True, height_cm=170.0, bmi=22.15)
        hold_b = dict(hold_a, height_cm=172.0, bmi=21.64)
        self.assertNotEqual(telemetry_change_key(hold_a), telemetry_change_key(hold_b))

    def test_format_telemetry_line_standby_active_and_result_hold(self):
        standby_line = format_telemetry_line({
            "phase": "STANDBY",
            "cpu_mhz": 80,
            "ble_duty_percent": 10.0,
            "weight_kg": None,
            "weight_stable": False,
            "lcd_backlight": False,
            "wifi_ps": "MIN_MODEM",
            "wifi_started": True,
            "light_sleep_enabled": True,
            "usb_sleep_blocked": False,
            "no_ls_locks": 0,
            "hold_sec": 10,
        })
        self.assertIn("[STANDBY] CPU=80MHz BLE=10.0%", standby_line)
        self.assertIn("LCD=OFF |", standby_line)
        self.assertNotIn("BMI=", standby_line)

        hold_line = format_telemetry_line({
            "phase": "RESULT_HOLD",
            "cpu_mhz": 80,
            "ble_duty_percent": 10.0,
            "weight_kg": 65.4,
            "weight_stable": True,
            "lcd_backlight": True,
            "wifi_ps": "MIN_MODEM",
            "wifi_started": True,
            "light_sleep_enabled": True,
            "usb_sleep_blocked": True,
            "no_ls_locks": 1,
            "hold_sec": 10,
            "height_cm": 172.0,
            "bmi": 22.11,
        })
        self.assertIn("[RESULT_HOLD] CPU=80MHz BLE=10.0%", hold_line)
        self.assertIn("LCD=ON H=172.0 cm BMI=22.11 HOLD=10s |", hold_line)

    def test_servo_sweep_and_locked_measure_telemetry(self):
        sweep_a = {
            "phase": "SERVO_SWEEP",
            "cpu_mhz": 240,
            "ble_duty_percent": 12.5,
            "weight_kg": 65.4,
            "weight_stable": False,
            "lcd_backlight": True,
            "wifi_ps": "NONE",
            "wifi_started": True,
            "light_sleep_enabled": True,
            "usb_sleep_blocked": True,
            "no_ls_locks": 2,
            "hold_sec": 10,
            "servo_deg": 107.5,
            "servo_attached": True,
        }
        sweep_b = dict(sweep_a, servo_deg=103.0)
        locked = dict(sweep_b, phase="LOCKED_MEASURE", servo_attached=False, height_cm=170.0, bmi=22.63)
        self.assertNotEqual(telemetry_change_key(sweep_a), telemetry_change_key(sweep_b))
        self.assertNotEqual(telemetry_change_key(sweep_b), telemetry_change_key(locked))

        sweep_line = format_telemetry_line(sweep_a)
        self.assertIn("[SERVO_SWEEP] CPU=240MHz BLE=12.5%", sweep_line)
        self.assertIn("LCD=ON SRV=107.5deg(ON) |", sweep_line)

        locked_line = format_telemetry_line(locked)
        self.assertIn("[LOCKED_MEASURE]", locked_line)
        self.assertIn("H=170.0 cm BMI=22.63 SRV=103.0deg(OFF) |", locked_line)


if __name__ == "__main__":
    unittest.main()
