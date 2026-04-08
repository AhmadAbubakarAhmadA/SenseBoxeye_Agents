"""Background MQTT subscriber that caches the latest sensor readings."""

import json
import threading
import time
from collections import deque

import paho.mqtt.client as mqtt

from agent.config import (
    MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS, MQTT_CLIENT_ID,
)


class MQTTReader:
    """Subscribe to sensors/+/data and cache readings with history."""

    def __init__(self, history_size: int = 60):
        self._latest: dict[str, dict] = {}          # sensor_id -> last reading
        self._history: dict[str, deque] = {}         # sensor_id -> deque of readings
        self._history_size = history_size
        self._lock = threading.Lock()
        self._connected = threading.Event()
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=MQTT_CLIENT_ID,
        )
        if MQTT_USER:
            self._client.username_pw_set(MQTT_USER, MQTT_PASS)
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message

    # -- callbacks --

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        if rc == 0 or rc.value == 0:
            client.subscribe("sensors/+/data")
            self._connected.set()

    def _on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload)
        except json.JSONDecodeError:
            return
        sid = payload.get("sensor_id", msg.topic.split("/")[1])
        payload["_recv_ts"] = time.time()
        with self._lock:
            self._latest[sid] = payload
            if sid not in self._history:
                self._history[sid] = deque(maxlen=self._history_size)
            self._history[sid].append(payload)

    # -- public API --

    def start(self, timeout: float = 10.0) -> bool:
        """Connect and start the background loop. Returns True on success."""
        self._client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        self._client.loop_start()
        return self._connected.wait(timeout)

    def stop(self):
        self._client.loop_stop()
        self._client.disconnect()

    def get_latest(self, sensor_id: str) -> dict | None:
        with self._lock:
            return self._latest.get(sensor_id)

    def get_all_latest(self) -> dict[str, dict]:
        with self._lock:
            return dict(self._latest)

    def get_history(self, sensor_id: str, last_n: int | None = None) -> list[dict]:
        with self._lock:
            h = self._history.get(sensor_id, deque())
            items = list(h)
        if last_n:
            items = items[-last_n:]
        return items

    def trend(self, sensor_id: str, field: str, window: int = 6) -> str:
        """Return 'rising', 'falling', or 'stable' over the last *window* readings."""
        hist = self.get_history(sensor_id, last_n=window)
        if len(hist) < 2:
            return "insufficient_data"
        vals = [r.get("data", {}).get(field) for r in hist if r.get("data", {}).get(field) is not None]
        if len(vals) < 2:
            return "insufficient_data"
        delta = vals[-1] - vals[0]
        if abs(delta) < 0.05 * abs(vals[0] or 1):
            return "stable"
        return "rising" if delta > 0 else "falling"
