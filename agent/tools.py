"""Tool definitions for Claude tool_use — mirrors the MCP schemas from the README."""

import json
from pathlib import Path

from agent.mqtt_reader import MQTTReader

_placements = json.loads(
    (Path(__file__).resolve().parent.parent / "spatial" / "sensor_placements.json").read_text()
)
_node_a = _placements["nodes"]["node_a"]

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
]

# ---------------------------------------------------------------------------
# Tool handlers
# ---------------------------------------------------------------------------

SENSOR_ID = _node_a["id"]  # "env_station_01"


def _envelope(data: dict) -> dict:
    """Wrap tool output with spatial metadata."""
    return {
        "sensor_id": SENSOR_ID,
        "location": _node_a["location"],
        "room": _node_a["room"],
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
    reading = reader.get_latest(SENSOR_ID)
    if reading is None:
        return json.dumps({"error": "No sensor data available yet. The sensor may still be warming up."})

    data = reading.get("data", {})

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

    elif name == "get_sensor_history":
        last_n = args.get("last_n", 10)
        hist = reader.get_history(SENSOR_ID, last_n=last_n)
        result = _envelope({
            "readings_count": len(hist),
            "readings": [h.get("data", {}) for h in hist],
        })

    else:
        result = {"error": f"Unknown tool: {name}"}

    return json.dumps(result)
