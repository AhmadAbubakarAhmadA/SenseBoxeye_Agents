"""Runtime configuration for the SpatialSenseAgent."""
import os
from pathlib import Path

# Minimal .env loader (no extra dependency)
_env_file = Path(__file__).resolve().parent.parent / ".env"
if _env_file.exists():
    for _line in _env_file.read_text().splitlines():
        _line = _line.strip()
        if not _line or _line.startswith("#") or "=" not in _line:
            continue
        _k, _v = _line.split("=", 1)
        os.environ.setdefault(_k.strip(), _v.strip())

# MQTT broker (local EMQX docker)
MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")
MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "spatial_agent")

# Topic conventions
TOPIC_DATA_PREFIX = "sensors"          # sensors/<node_id>/data
TOPIC_MCP_PREFIX = "$mcp-rpc"          # $mcp-rpc/<client>/<server>/<name>

# LLM
ANTHROPIC_API_KEY = os.getenv("ANTHROPIC_API_KEY", "")
LLM_MODEL = os.getenv("LLM_MODEL", "claude-sonnet-4-5-20241022")

# Paths
PROJECT_ROOT = Path(__file__).resolve().parent.parent
SPATIAL_FILE = PROJECT_ROOT / "spatial" / "sensor_placements.json"
