"""Build GeoJSON FeatureCollections from sensor readings."""

import json
from pathlib import Path
from datetime import datetime, timezone

_placements = json.loads(
    (Path(__file__).resolve().parent.parent / "spatial" / "sensor_placements.json").read_text()
)


def _node_meta(sensor_id: str) -> dict | None:
    for node in _placements["nodes"].values():
        if node["id"] == sensor_id:
            return node
    return None


def reading_to_feature(reading: dict) -> dict:
    """Convert a single sensor reading dict to a GeoJSON Feature."""
    sid = reading.get("sensor_id", "unknown")
    meta = _node_meta(sid) or {}
    loc = reading.get("location", meta.get("location", {}))
    data = reading.get("data", {})

    return {
        "type": "Feature",
        "geometry": {
            "type": "Point",
            "coordinates": [loc.get("x", 0), loc.get("y", 0)],
        },
        "properties": {
            "sensor_id": sid,
            "room": reading.get("room", meta.get("room", "")),
            "floor": loc.get("floor", 1),
            "z": loc.get("z", 0),
            "timestamp": datetime.now(timezone.utc).isoformat(),
            **data,
        },
    }


def readings_to_collection(readings: list[dict]) -> dict:
    """Convert a list of readings to a GeoJSON FeatureCollection."""
    return {
        "type": "FeatureCollection",
        "features": [reading_to_feature(r) for r in readings],
    }


def snapshot_geojson(latest_map: dict[str, dict]) -> str:
    """Take all latest readings and return a pretty-printed GeoJSON string."""
    fc = readings_to_collection(list(latest_map.values()))
    return json.dumps(fc, indent=2)
