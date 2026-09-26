"""Appends gateway health to a CSV at a fixed interval, for soak tests.

Usage:
    python status_logger.py <host> <username> <output.csv> [interval_seconds]

The password is read from the OSK_PASSWORD environment variable, or asked for
when it is not set. The session
lives in gateway RAM, so the logger signs in again after a gateway restart;
a changed boot_id in the CSV marks the restart.
"""

import csv
import getpass
import http.cookiejar
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

RADIO_COUNTERS = (
    "interrupts",
    "missed_interrupts",
    "module_restores",
    "module_restore_failures",
    "packets",
    "ack_requests_ignored",
    "telemetry_acks_sent",
    "telemetry_rejected_inactive",
    "telemetry_frames_queued",
    "telemetry_frames_dropped",
    "power_targets_sent",
)
COLUMNS = (
    "logged_at",
    "boot_id",
    "reset_reason",
    "uptime_seconds",
    "free_heap",
    "radio_state",
    *RADIO_COUNTERS,
    "telemetry_updates",
    "websocket_clients",
    "websocket_messages_sent",
    "websocket_messages_dropped",
    "error",
)


class Gateway:
    def __init__(self, host, username, password):
        self.base = f"http://{host}"
        self.credentials = json.dumps(
            {"username": username, "password": password}).encode()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def sign_in(self):
        request = urllib.request.Request(
            f"{self.base}/ui/session", data=self.credentials,
            headers={"Content-Type": "application/json"}, method="POST")
        self.opener.open(request, timeout=30).read()

    def status(self):
        try:
            return self._get_status()
        except urllib.error.HTTPError as error:
            if error.code != 401:
                raise
        self.sign_in()
        return self._get_status()

    def _get_status(self):
        with self.opener.open(f"{self.base}/ui/status", timeout=10) as response:
            return json.load(response)


def row_from(status):
    radio = status.get("radio", {})
    counters = radio.get("counters", {})
    telemetry = status.get("telemetry", {})
    websocket = status.get("websocket", {})
    row = {
        "boot_id": status.get("boot_id"),
        "reset_reason": status.get("reset_reason"),
        "uptime_seconds": status.get("uptime_seconds"),
        "free_heap": status.get("free_heap"),
        "radio_state": radio.get("state"),
        "telemetry_updates": telemetry.get("updates"),
        "websocket_clients": websocket.get("clients"),
        "websocket_messages_sent": websocket.get("messages_sent"),
        "websocket_messages_dropped": websocket.get("messages_dropped"),
    }
    for name in RADIO_COUNTERS:
        row[name] = counters.get(name)
    return row


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    host, username, output = sys.argv[1:4]
    password = os.environ.get("OSK_PASSWORD") or getpass.getpass(
        f"Password for {username}@{host}: ")
    interval = int(sys.argv[4]) if len(sys.argv) == 5 else 300
    gateway = Gateway(host, username, password)

    new_file = not os.path.exists(output)
    with open(output, "a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS)
        if new_file:
            writer.writeheader()
        while True:
            started = time.monotonic()
            try:
                row = row_from(gateway.status())
            except (OSError, ValueError) as error:
                # An unreachable gateway is itself a result worth a row.
                row = {"error": str(error)}
            row["logged_at"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
            writer.writerow(row)
            handle.flush()
            print(", ".join(f"{key}={row.get(key)}" for key in (
                "logged_at", "uptime_seconds", "free_heap", "packets",
                "missed_interrupts", "module_restores", "telemetry_frames_dropped",
                "error")))
            time.sleep(max(0.0, interval - (time.monotonic() - started)))


if __name__ == "__main__":
    main()
