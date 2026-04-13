"""Tool definitions for Claude tool_use — mirrors the MCP schemas from the README."""

import json
from pathlib import Path

from agent.mqtt_reader import MQTTReader

_placements = json.loads(
    (Path(__file__).resolve().parent.parent / "spatial" / "sensor_placements.json").read_text()
)
_node_a = _placements["nodes"]["node_a"]
_node_c = _placements["nodes"]["node_c"]

# ---------------------------------------------------------------------------
# Tool schemas (Anthropic tool_use format)
# ---------------------------------------------------------------------------

TOOL_DEFINITIONS = [
    {
        "name": "read_air_quality",
        "description": (
            "Read all environmental parameters from the SEN66 air quality sensor "
            "in Lab B. Returns PM1, PM2.5, PM4, PM10, temperature, humidity, "
            "VOC index, NOx index, and CO2. Sensor is at position (6.0, 6.0) "
            "in Lab B at 1.2m height."
        ),
        "input_schema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
    {
        "name": "get_co2_level",
        "description": (
            "Get the current CO2 concentration in ppm from the SEN66 sensor in "
            "Lab B at position (6.0, 6.0). Also returns a trend indicator "
            "(rising/falling/stable) based on recent readings."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "include_trend": {
                    "type": "boolean",
                    "description": "Whether to include trend data",
                },
            },
            "required": [],
        },
    },
    {
        "name": "get_comfort_index",
        "description": (
            "Compute an indoor comfort index for Lab B based on temperature, "
            "humidity, and CO2 from the SEN66 sensor at position (6.0, 6.0). "
            "Returns a score from 0 (very uncomfortable) to 100 (ideal) and "
            "individual factor scores."
        ),
        "input_schema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
    {
        "name": "detect_motion",
        "description": (
            "Check the IMU (ICM-20948) on the environment station in Lab B at "
            "position (6.0, 6.0) for motion. Returns the latest accelerometer "
            "magnitude in g (1.0 g = stationary, gravity only), the deviation "
            "from rest, the latest gyroscope rates (deg/s) per axis, and a "
            "boolean 'motion_detected' flag (true if accel deviation > 0.15 g "
            "or any gyro axis > 20 deg/s)."
        ),
        "input_schema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
    {
        "name": "get_sensor_history",
        "description": (
            "Get the last N readings from the SEN66 sensor in Lab B at "
            "position (6.0, 6.0). Useful for identifying temporal patterns."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "last_n": {
                    "type": "integer",
                    "description": "Number of recent readings to return (default: 10)",
                },
            },
            "required": [],
        },
    },
    # --- Node C: door sentinel tools ---
    {
        "name": "detect_vibration_event",
        "description": (
            "Get recent vibration events detected by the IMU on the door frame "
            "between Lab B and the hallway, at position (9.5, 4.0). Each event "
            "represents a door opening or closing."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "last_n": {
                    "type": "integer",
                    "description": "Number of recent events to return (default: 20)",
                },
                "since_minutes": {
                    "type": "integer",
                    "description": "Only return events from the last N minutes",
                },
            },
            "required": [],
        },
    },
    {
        "name": "get_door_state",
        "description": (
            "Get inferred door state (open/closed) for the door between Lab B "
            "and hallway at position (9.5, 4.0), based on vibration pattern "
            "analysis."
        ),
        "input_schema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
]

# ---------------------------------------------------------------------------
# Tool handlers
# ---------------------------------------------------------------------------

SENSOR_ID = _node_a["id"]  # "env_station_01"
DOOR_SENSOR_ID = _node_c["id"]  # "door_sentinel_01"


def _envelope(data: dict, node=None) -> dict:
    """Wrap tool output with spatial metadata."""
    n = node or _node_a
    return {
        "sensor_id": n["id"],
        "location": n["location"],
        "room": n["room"],
        **data,
    }


def _comfort_score(temp: float, rh: float, co2: float) -> dict:
    """Simple comfort index: each factor 0-100, averaged."""
    # Temperature (ideal 21-23)
    if 21 <= temp <= 23:
        t_score = 100
    elif 20 <= temp < 21 or 23 < temp <= 24:
        t_score = 80
    elif 18 <= temp < 20 or 24 < temp <= 26:
        t_score = 50
    else:
        t_score = 20

    # Humidity (ideal 40-55)
    if 40 <= rh <= 55:
        h_score = 100
    elif 30 <= rh < 40 or 55 < rh <= 60:
        h_score = 70
    elif 25 <= rh < 30 or 60 < rh <= 70:
        h_score = 40
    else:
        h_score = 15

    # CO2 (ideal <600)
    if co2 < 600:
        c_score = 100
    elif co2 < 800:
        c_score = 80
    elif co2 < 1200:
        c_score = 50
    else:
        c_score = max(0, 30 - (co2 - 1200) / 100)

    overall = round((t_score + h_score + c_score) / 3)
    label = "good" if overall >= 70 else "acceptable" if overall >= 45 else "poor"
    return {
        "comfort_score": overall,
        "comfort_label": label,
        "factors": {
            "temperature_score": t_score,
            "humidity_score": h_score,
            "co2_score": round(c_score),
        },
    }


def handle_tool(name: str, args: dict, reader: MQTTReader) -> str:
    """Dispatch a tool call and return JSON string result."""
    # Node C tools don't need Node A data
    _NODE_C_TOOLS = {"detect_vibration_event", "get_door_state"}

    if name not in _NODE_C_TOOLS:
        reading = reader.get_latest(SENSOR_ID)
        if reading is None:
            return json.dumps({"error": "No sensor data available yet. The sensor may still be warming up."})
        data = reading.get("data", {})
    else:
        reading = None
        data = {}

    if name == "read_air_quality":
        result = _envelope({"data": data})

    elif name == "get_co2_level":
        result = _envelope({
            "co2_ppm": data.get("co2_ppm"),
        })
        if args.get("include_trend", False):
            result["trend"] = {
                "co2": reader.trend(SENSOR_ID, "co2_ppm"),
                "temperature": reader.trend(SENSOR_ID, "temperature_c"),
            }

    elif name == "get_comfort_index":
        comfort = _comfort_score(
            data.get("temperature_c", 0),
            data.get("humidity_rh", 0),
            data.get("co2_ppm", 0),
        )
        result = _envelope({
            "data": {
                "temperature_c": data.get("temperature_c"),
                "humidity_rh": data.get("humidity_rh"),
                "co2_ppm": data.get("co2_ppm"),
            },
            **comfort,
        })

    elif name == "detect_motion":
        ax = data.get("accel_x_g", 0.0)
        ay = data.get("accel_y_g", 0.0)
        az = data.get("accel_z_g", 0.0)
        gx = data.get("gyro_x_dps", 0.0)
        gy = data.get("gyro_y_dps", 0.0)
        gz = data.get("gyro_z_dps", 0.0)
        accel_mag = (ax * ax + ay * ay + az * az) ** 0.5
        accel_dev = abs(accel_mag - 1.0)
        max_gyro = max(abs(gx), abs(gy), abs(gz))
        motion = accel_dev > 0.15 or max_gyro > 20.0
        result = _envelope({
            "motion_detected": motion,
            "accel_magnitude_g": round(accel_mag, 3),
            "accel_deviation_g": round(accel_dev, 3),
            "gyro_dps": {"x": gx, "y": gy, "z": gz},
            "max_gyro_dps": round(max_gyro, 2),
        })

    elif name == "get_sensor_history":
        last_n = args.get("last_n", 10)
        hist = reader.get_history(SENSOR_ID, last_n=last_n)
        result = _envelope({
            "readings_count": len(hist),
            "readings": [h.get("data", {}) for h in hist],
        })

    elif name == "detect_vibration_event":
        door_reading = reader.get_latest(DOOR_SENSOR_ID)
        if door_reading is None:
            return json.dumps({"error": "No door sentinel data available yet."})
        door_data = door_reading.get("data", {})
        events = door_data.get("events", [])
        last_n = args.get("last_n", 20)
        since_minutes = args.get("since_minutes")
        if since_minutes:
            # Filter events by recency using the node's uptime timestamp
            node_ts = door_reading.get("timestamp_ms", 0)
            cutoff = node_ts - since_minutes * 60 * 1000
            events = [e for e in events if e.get("ts", 0) >= cutoff]
        events = events[-last_n:]
        result = _envelope({
            "event_count": len(events),
            "events": events,
        }, node=_node_c)

    elif name == "get_door_state":
        door_reading = reader.get_latest(DOOR_SENSOR_ID)
        if door_reading is None:
            return json.dumps({"error": "No door sentinel data available yet."})
        door_data = door_reading.get("data", {})
        result = _envelope({
            "door_state": door_data.get("door_state", "unknown"),
            "event_count": door_data.get("event_count", 0),
            "last_event_ms": door_data.get("last_event_ms", 0),
        }, node=_node_c)

    else:
        result = {"error": f"Unknown tool: {name}"}

    return json.dumps(result)
