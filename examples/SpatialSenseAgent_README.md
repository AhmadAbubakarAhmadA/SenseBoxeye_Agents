# SpatialSenseAgent

**An agentic LLM system that discovers, orchestrates, and spatially reasons about IoT sensors using MCP-over-MQTT on ESP32-S3.**

Each ESP32 sensor node registers its capabilities as MCP (Model Context Protocol) tools over MQTT. An LLM agent discovers these tools at runtime, queries sensors through natural language, reasons about spatial relationships between readings, and outputs GeoJSON for indoor environment visualization.

---

## Table of contents

1. [Concept](#concept)
2. [Architecture](#architecture)
3. [Hardware inventory](#hardware-inventory)
4. [Node configuration](#node-configuration)
5. [Software stack](#software-stack)
6. [Spatial coordinate system](#spatial-coordinate-system)
7. [MCP tool schemas](#mcp-tool-schemas)
8. [LLM agent design](#llm-agent-design)
9. [GeoJSON output format](#geojson-output-format)
10. [Build plan](#build-plan)
11. [Project structure](#project-structure)
12. [References](#references)

---

## Concept

Traditional IoT dashboards display sensor data. This project goes further: an LLM agent **autonomously decides** which sensors to query, **correlates** readings across spatially distributed nodes, and **generates** natural language spatial analysis grounded in real data.

The key innovation is embedding **spatial metadata** (coordinates, room ID, heading, FOV) directly into each sensor's MCP tool schema. The LLM doesn't need hardcoded knowledge of the building layout — it discovers sensor locations through the same protocol it uses to discover sensor capabilities.

**What makes this a spatial/GIS project:**
- Sensor readings are geolocated to indoor coordinates
- The LLM performs spatial reasoning (gradients, proximity, clustering)
- Output is standards-compliant GeoJSON for QGIS/Leaflet visualization
- Environmental parameters are analyzed as spatial phenomena, not isolated data points

**What makes this an agentic AI project:**
- The LLM chooses which sensors to query based on the user's question
- Multi-step reasoning chains: query → interpret → query another sensor → correlate → respond
- New sensors auto-register via MCP — the agent discovers them without code changes
- The agent explains causation ("CO₂ is high because the door hasn't opened in 3 hours")

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    SENSOR LAYER (ESP32-S3)                   │
│                   Each node = MCP Server                     │
│                                                              │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐         │
│  │   Node A      │ │   Node B      │ │   Node C      │ ...   │
│  │ SEN66 + IMU   │ │ Camera + IMU  │ │ IMU (door)    │       │
│  │ Air quality   │ │ Visual scene  │ │ Event detect  │       │
│  │ station       │ │ observer      │ │ sentinel      │       │
│  └──────┬───────┘ └──────┬───────┘ └──────┬───────┘         │
│         │                │                │                  │
│    MCP tool           MCP tool        MCP tool               │
│    registration       registration    registration           │
└─────────┼────────────────┼────────────────┼──────────────────┘
          │                │                │
          ▼                ▼                ▼
┌─────────────────────────────────────────────────────────────┐
│              TRANSPORT LAYER (MQTT + MCP)                    │
│                                                              │
│  EMQX Serverless Broker (free tier)                         │
│  Topics: $mcp-rpc/{client}/{server}/{name}                  │
│  Protocol: MCP over MQTT (JSON-RPC 2.0)                     │
│                                                              │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│               AGENT LAYER (Python)                           │
│                                                              │
│  ┌────────────┐  ┌────────────┐  ┌─────────────────┐       │
│  │ MCP Client  │  │ LLM Engine │  │ Spatial Logic    │       │
│  │ (LlamaIndex)│→│ (Claude /  │→│ Coord system     │       │
│  │ Tool disco- │  │  GPT-4 /   │  │ Distance calc    │       │
│  │ very & call │  │  picoLLM)  │  │ GeoJSON builder  │       │
│  └────────────┘  └────────────┘  └─────────────────┘       │
│                                                              │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│               OUTPUT LAYER                                   │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ NL Reports    │  │ GeoJSON      │  │ Live Dashboard│      │
│  │ "CO₂ high in │  │ FeatureCol-  │  │ Leaflet or    │      │
│  │  Lab B, door  │  │ lection for  │  │ QGIS floor    │      │
│  │  closed 3hrs" │  │ QGIS         │  │ plan overlay  │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

---

## Hardware inventory

### Currently available

| Component | Qty | Interface | Key parameters |
|-----------|-----|-----------|----------------|
| ESP32-S3 (8MB PSRAM) | 4+ | — | Dual-core 240MHz, Wi-Fi, BLE 5, I2S mic, vector AI |
| Sensirion SEN66 | 1 | I2C (0x6B) | PM1/2.5/4/10, Temperature, Humidity, VOC, NOx, CO₂ |
| senseBox Eye (BMX055 IMU) | 4 | I2C | 3-axis accel + 3-axis gyro + 3-axis magnetometer |
| OV2640 Camera (66° FOV) | 2 | DVP/SPI | Up to 1600×1200 JPEG |

### Future addition (auto-discovered via MCP)

| Component | Qty | Interface | Key parameters |
|-----------|-----|-----------|----------------|
| AMG8833 Thermal 8×8 | 1 | I2C (0x69) | 64-pixel thermal grid, 0–80°C, ±2.5°C, 10fps, 7m range |

---

## Node configuration

### Node A — Environment station (primary)

**Hardware:** ESP32-S3 + SEN66 + 1× senseBox Eye IMU

**Location:** Center of monitored room, on a shelf at 1.2m height (breathing zone, per ASHRAE guidelines).

**Wiring:**
```
ESP32-S3          SEN66 (I2C)        BMX055 IMU (I2C)
─────────         ──────────         ─────────────────
GPIO 21 (SDA) ──→ SDA                SDA
GPIO 22 (SCL) ──→ SCL                SCL
3.3V          ──→ VIN (3.3V only!)   VIN
GND           ──→ GND                GND
```

**Important:** The SEN66 maximum supply voltage is 3.6V. Do NOT connect to 5V — it will damage the sensor. Both SEN66 (0x6B) and BMX055 (accel: 0x18, gyro: 0x68, mag: 0x10) share the same I2C bus at different addresses.

**MCP tools registered:**
- `read_air_quality` — returns all 9 SEN66 parameters + location
- `get_co2_level` — returns CO₂ ppm + trend (rising/falling/stable)
- `get_comfort_index` — returns computed comfort score based on temp + humidity + CO₂
- `detect_motion` — returns IMU accelerometer events above threshold

**Sampling:** SEN66 every 5 seconds, IMU motion detection continuous (interrupt-driven).

---

### Node B — Visual observer

**Hardware:** ESP32-S3 (with onboard OV2640) + 1× senseBox Eye IMU

**Location:** Corner of room, mounted high (2m), angled toward the entry/window area.

**MCP tools registered:**
- `capture_scene` — takes JPEG photo, returns base64 + timestamp + heading
- `detect_objects` — runs on-device Edge Impulse model (optional), returns bounding boxes
- `get_orientation` — returns IMU compass heading (confirms camera aim direction)

**Sampling:** Camera on-demand only (triggered by MCP tool call). IMU orientation on request.

---

### Node C — Door/window sentinel

**Hardware:** ESP32-S3 + 1× senseBox Eye IMU

**Location:** Mounted on door frame with adhesive. Detects door open/close via accelerometer vibration spike.

**MCP tools registered:**
- `detect_vibration_event` — returns last N vibration events with timestamps
- `get_door_state` — returns inferred state (open/closed) based on vibration pattern analysis

**Sampling:** Accelerometer interrupt-driven. Event buffer stores last 50 events with timestamps.

---

### Node D — Second location (optional, for spatial comparison)

**Hardware:** ESP32-S3 (with onboard OV2640) + 1× senseBox Eye IMU

**Location:** Hallway or second room. Provides spatial contrast point.

**MCP tools registered:**
- `capture_scene` — same as Node B
- `detect_motion` — same as Node A

---

## Software stack

### Firmware (ESP32-S3)

| Component | Version | Purpose |
|-----------|---------|---------|
| ESP-IDF | v5.x | Development framework (required for MCP component) |
| mcp-over-mqtt component | Latest | MCP server on ESP32, tool registration via MQTT |
| Sensirion arduino-i2c-sen66 | Latest | SEN66 I2C driver (ported to ESP-IDF or used via Arduino-as-component) |
| Adafruit BMX055 / BMX160 | Latest | IMU driver |
| esp32-camera | Latest | OV2640 JPEG capture |
| Edge Impulse SDK | Optional | On-device object detection |

### MQTT Broker

| Component | Tier | Limits |
|-----------|------|--------|
| EMQX Cloud Serverless | Free (spend limit $0) | 1M session-min/mo, 1GB traffic, TLS required (port 8883) |

With 4 nodes connected 24/7: ~173K session-min/month (17% of free quota). Approximately 130 messages/hour/client continuous — more than enough.

### Agent (Python)

| Component | Version | Purpose |
|-----------|---------|---------|
| Python | 3.10+ | Runtime |
| LlamaIndex | Latest | MCP client, tool orchestration, agent framework |
| mcp-over-mqtt (Python SDK) | Latest | MQTT transport for MCP |
| paho-mqtt | Latest | MQTT client (fallback if needed) |
| geojson | Latest | GeoJSON generation |
| Anthropic SDK / OpenAI SDK | Latest | LLM API client |

### Visualization

| Component | Purpose |
|-----------|---------|
| QGIS | Indoor floor plan with GeoJSON overlay |
| Leaflet.js | Web-based live dashboard (optional) |

---

## Spatial coordinate system

### Indoor coordinate reference

We use a simple local Cartesian coordinate system (meters from origin).

```
Origin (0,0) = Southwest corner of the building floor
X-axis = East (increasing)
Y-axis = North (increasing)
Z-axis = Height above floor (meters)

Room: Lab B
  Northwest corner: (2.0, 9.0)
  Southeast corner: (10.0, 3.0)
  Dimensions: 8m (E-W) × 6m (N-S)
  Floor: 1

Hallway:
  West end: (10.0, 1.0)
  East end: (18.0, 1.0)
  Width: 2m
```

### Sensor placement coordinates

```json
{
  "node_a": {
    "id": "env_station_01",
    "location": {"x": 6.0, "y": 6.0, "z": 1.2, "floor": 1},
    "room": "lab_b",
    "description": "Center of Lab B, shelf height"
  },
  "node_b": {
    "id": "visual_observer_01",
    "location": {"x": 2.5, "y": 8.5, "z": 2.0, "floor": 1},
    "room": "lab_b",
    "heading_deg": 135,
    "fov_deg": 66,
    "description": "NW corner of Lab B, aimed at entry"
  },
  "node_c": {
    "id": "door_sentinel_01",
    "location": {"x": 9.5, "y": 4.0, "z": 2.1, "floor": 1},
    "room": "lab_b",
    "description": "Door frame between Lab B and hallway"
  },
  "node_d": {
    "id": "hallway_observer_01",
    "location": {"x": 12.0, "y": 1.0, "z": 2.0, "floor": 1},
    "room": "hallway",
    "heading_deg": 270,
    "fov_deg": 66,
    "description": "Hallway, aimed west toward Lab B door"
  }
}
```

These coordinates are embedded in each node's MCP tool schemas so the LLM knows where every sensor is.

---

## MCP tool schemas

### Node A — Environment station

```json
{
  "name": "read_air_quality",
  "description": "Read all environmental parameters from the SEN66 air quality sensor in Lab B. Returns PM1, PM2.5, PM4, PM10, temperature, humidity, VOC index, NOx index, and CO2. Sensor is at position (6.0, 6.0) in Lab B at 1.2m height.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "required": []
  }
}
```

```json
{
  "name": "get_co2_level",
  "description": "Get the current CO2 concentration in ppm from the SEN66 sensor in Lab B at position (6.0, 6.0). Also returns a trend indicator (rising/falling/stable) based on the last 5 minutes of readings.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "include_trend": {
        "type": "boolean",
        "description": "Whether to include 5-minute trend data"
      }
    },
    "required": []
  }
}
```

```json
{
  "name": "get_comfort_index",
  "description": "Compute an indoor comfort index for Lab B based on temperature, humidity, and CO2 from the SEN66 sensor at position (6.0, 6.0). Returns a score from 0 (very uncomfortable) to 100 (ideal) and individual factor scores.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "required": []
  }
}
```

```json
{
  "name": "detect_motion",
  "description": "Check for recent motion events detected by the IMU accelerometer at the environment station in Lab B, position (6.0, 6.0). Returns the last N motion events with timestamps and magnitude.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "last_n": {
        "type": "integer",
        "description": "Number of recent events to return (default: 10)"
      }
    },
    "required": []
  }
}
```

### Node B — Visual observer

```json
{
  "name": "capture_scene",
  "description": "Capture a JPEG photo from the camera in the NW corner of Lab B at position (2.5, 8.5), aimed at 135° (toward the room entry and south window). FOV is 66°. Returns base64-encoded JPEG image.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "resolution": {
        "type": "string",
        "enum": ["low", "medium", "high"],
        "description": "Image resolution: low=320x240, medium=640x480, high=1024x768"
      }
    },
    "required": []
  }
}
```

### Node C — Door sentinel

```json
{
  "name": "detect_vibration_event",
  "description": "Get recent vibration events detected by the IMU on the door frame between Lab B and the hallway, at position (9.5, 4.0). Each event represents a door opening or closing. Returns timestamps and vibration magnitude.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "last_n": {
        "type": "integer",
        "description": "Number of recent events to return (default: 20)"
      },
      "since_minutes": {
        "type": "integer",
        "description": "Only return events from the last N minutes"
      }
    },
    "required": []
  }
}
```

### Tool response format (all nodes)

Every tool response includes a standardized spatial envelope:

```json
{
  "sensor_id": "env_station_01",
  "timestamp": "2026-04-07T14:32:15Z",
  "location": {
    "x": 6.0, "y": 6.0, "z": 1.2,
    "floor": 1, "room": "lab_b"
  },
  "data": {
    "co2_ppm": 1840,
    "temperature_c": 24.2,
    "humidity_rh": 68.1,
    "voc_index": 312,
    "nox_index": 45,
    "pm2_5": 8.3,
    "pm10": 12.1
  },
  "trend": {
    "co2_5min": "rising",
    "temperature_5min": "stable"
  }
}
```

---

## LLM agent design

### System prompt

```
You are a spatial indoor environment analyst. You have access to IoT sensors
deployed across an indoor space. Each sensor is registered as a tool with
location metadata (x, y, z coordinates in meters, room name, and floor number).

SPATIAL CONTEXT:
- Coordinate system: local Cartesian, origin at SW corner of building
- X-axis = East, Y-axis = North, Z = height above floor
- Room "lab_b": x:[2-10], y:[3-9], 8m×6m, floor 1
- Hallway: x:[10-18], y:[0-2], floor 1
- Door between lab_b and hallway at approximately (9.5, 4.0)

BEHAVIOR:
1. When answering questions, call the relevant sensor tools to get REAL data.
   Never invent or assume sensor readings.
2. When comparing data from multiple sensors, calculate the distance between
   them and note their relative positions (e.g., "the sensor near the door
   reads 3°C cooler than the one in the center").
3. Identify spatial patterns: gradients (temperature/CO2 decreasing toward
   the window), anomalies (unexpected readings at a specific location),
   and correlations (door event followed by CO2 change).
4. Reference physical landmarks in your analysis: "near the south window",
   "by the door to the hallway", "center of Lab B".
5. When asked for spatial data, output valid GeoJSON FeatureCollections
   using the sensor coordinates.

COMFORT THRESHOLDS:
- CO2: <800 ppm = good, 800-1200 = acceptable, >1200 = poor ventilation
- Temperature: 20-24°C = comfortable (heating season), 23-26°C (cooling season)
- Humidity: 30-60% RH = comfortable
- VOC index: <100 = clean, 100-250 = acceptable, >250 = poor
- PM2.5: <12 µg/m³ = good, 12-35 = moderate, >35 = unhealthy

Always ground your spatial reasoning in actual tool call results.
```

### Agent reasoning patterns

The agent follows agentic reasoning patterns depending on the query type:

**Spatial status query** ("How is the air in Lab B?")
→ Single tool call → Interpret against thresholds → Report with location context

**Spatial correlation query** ("Why is it stuffy?")
→ Multi-tool chain: air quality → door events → camera → synthesize causal explanation

**Spatial comparison query** ("Compare the lab and the hallway")
→ Parallel tool calls to both locations → Calculate spatial gradient → GeoJSON output

**Temporal-spatial query** ("What happened in the last hour?")
→ Request event logs from multiple sensors → Reconstruct timeline with spatial annotations

---

## GeoJSON output format

### Point features (sensor readings)

```json
{
  "type": "FeatureCollection",
  "features": [
    {
      "type": "Feature",
      "geometry": {
        "type": "Point",
        "coordinates": [6.0, 6.0]
      },
      "properties": {
        "sensor_id": "env_station_01",
        "room": "lab_b",
        "co2_ppm": 1840,
        "temperature_c": 24.2,
        "humidity_rh": 68.1,
        "voc_index": 312,
        "comfort_score": 32,
        "comfort_label": "poor",
        "timestamp": "2026-04-07T14:32:15Z"
      }
    }
  ]
}
```

### Event features (door/motion events)

```json
{
  "type": "Feature",
  "geometry": {
    "type": "Point",
    "coordinates": [9.5, 4.0]
  },
  "properties": {
    "sensor_id": "door_sentinel_01",
    "event_type": "door_open",
    "timestamp": "2026-04-07T14:07:22Z",
    "magnitude": 2.4
  }
}
```

### Spatial analysis features (agent-generated)

```json
{
  "type": "Feature",
  "geometry": {
    "type": "LineString",
    "coordinates": [[6.0, 6.0], [9.5, 4.0]]
  },
  "properties": {
    "analysis_type": "co2_gradient",
    "from_sensor": "env_station_01",
    "to_sensor": "door_sentinel_01",
    "co2_delta": -420,
    "distance_m": 4.1,
    "gradient_ppm_per_m": -102.4,
    "description": "CO2 decreases toward the door"
  }
}
```

---

## Build plan

### Phase 0 — Setup (Day 1)

- [ ] Create EMQX Cloud Serverless account, deploy free broker
- [ ] Note broker URL, port (8883), create auth credentials
- [ ] Install ESP-IDF v5.x on development machine
- [ ] Install Python 3.10+ with LlamaIndex, paho-mqtt
- [ ] Clone the `mqtt-ai/esp32-mcp-mqtt-tutorial` reference repo
- [ ] Test MQTT connectivity with MQTTX desktop client
- [ ] Get LLM API key (Anthropic Claude or OpenAI)

### Phase 1 — First sensor node over MQTT (Days 2-4)

- [ ] Wire ESP32-S3 + SEN66 on breadboard (I2C: SDA→21, SCL→22, 3.3V, GND)
- [ ] Flash basic SEN66 reading firmware (Arduino-as-component or pure ESP-IDF)
- [ ] Verify sensor readings on serial monitor (CO₂, temp, humidity, PM, VOC, NOx)
- [ ] Add MQTT client to firmware, publish JSON readings to topic `sensors/node_a/data`
- [ ] Verify messages arrive in MQTTX
- [ ] Add BMX055 IMU reading to same node (shared I2C bus)
- [ ] Implement motion detection threshold on accelerometer

**Milestone:** Raw sensor data flowing from ESP32-S3 to MQTT broker.

### Phase 2 — MCP tool registration (Days 5-7)

- [ ] Integrate `mcp-over-mqtt` ESP-IDF component into Node A firmware
- [ ] Register `read_air_quality` tool with JSON schema + spatial metadata
- [ ] Register `get_co2_level` tool with trend calculation
- [ ] Register `get_comfort_index` tool with comfort scoring logic
- [ ] Register `detect_motion` tool
- [ ] Write Python MCP client that discovers Node A's tools
- [ ] Verify: Python client can list tools and call them, receiving structured responses

**Milestone:** Node A's tools discoverable and callable via MCP protocol.

### Phase 3 — Add camera and door nodes (Days 8-10)

- [ ] Flash Node B: ESP32-S3 with OV2640 + IMU, register `capture_scene` + `get_orientation`
- [ ] Flash Node C: ESP32-S3 + IMU on door frame, register `detect_vibration_event`
- [ ] Verify all three nodes appear in Python MCP client's tool list simultaneously
- [ ] Test calling tools across different nodes from Python

**Milestone:** 3 sensor nodes with 7+ tools, all auto-discovered by MCP client.

### Phase 4 — LLM agent integration (Days 11-14)

- [ ] Build LlamaIndex agent that loads MCP tools as LLM-callable functions
- [ ] Write spatial reasoning system prompt (see LLM agent design section)
- [ ] Connect agent to LLM API (Claude or GPT-4)
- [ ] Test basic queries: "What's the CO₂ level?", "Is anyone in the room?"
- [ ] Test multi-step reasoning: "Why does it feel stuffy?"
- [ ] Test spatial comparison: "How does Lab B compare to the hallway?" (requires Node D)
- [ ] Iterate on system prompt based on LLM response quality

**Milestone:** Working agentic loop — natural language in, spatial analysis out.

### Phase 5 — GeoJSON output + visualization (Days 15-17)

- [ ] Add GeoJSON generation to agent output pipeline
- [ ] Create indoor floor plan image (simple SVG or scanned sketch)
- [ ] Georeference floor plan in QGIS (assign local coordinates to image corners)
- [ ] Load agent's GeoJSON as a vector layer on top of floor plan
- [ ] Style with graduated symbols (CO₂: blue→red, comfort: green→red)
- [ ] Optional: build Leaflet web dashboard with auto-refresh

**Milestone:** Live indoor environment map with spatial sensor data overlay.

### Phase 6 — Stretch goals (Days 18+)

- [ ] Add the AMG8833 thermal sensor as a new node — verify auto-discovery
- [ ] Implement voice interaction (ESP32 I2S mic → Whisper → agent → TTS)
- [ ] Deploy edge LLM on Raspberry Pi (picoLLM or PicoLM) for offline operation
- [ ] Add temporal analysis (rolling 24h logs, pattern detection)
- [ ] Multi-room deployment with 4+ nodes

---

## Project structure

```
SpatialSenseAgent/
├── README.md                          # This file
├── docs/
│   ├── architecture.md                # Detailed architecture docs
│   ├── references.md                  # Annotated bibliography (40+ sources)
│   └── floor_plan.svg                 # Indoor floor plan with coordinates
│
├── firmware/
│   ├── common/
│   │   ├── mcp_server/                # MCP-over-MQTT component (shared)
│   │   ├── mqtt_config/               # MQTT broker connection config
│   │   └── spatial_meta/              # Spatial metadata definitions
│   │
│   ├── node_a_environment/            # SEN66 + IMU environment station
│   │   ├── main/
│   │   │   ├── main.c                 # Entry point
│   │   │   ├── sen66_driver.c         # SEN66 I2C driver
│   │   │   ├── imu_driver.c           # BMX055 I2C driver
│   │   │   ├── mcp_tools.c            # MCP tool definitions & handlers
│   │   │   └── comfort_calc.c         # Comfort index calculation
│   │   ├── CMakeLists.txt
│   │   └── sdkconfig.defaults
│   │
│   ├── node_b_camera/                 # OV2640 + IMU visual observer
│   │   ├── main/
│   │   │   ├── main.c
│   │   │   ├── camera_driver.c
│   │   │   ├── imu_driver.c
│   │   │   └── mcp_tools.c
│   │   ├── CMakeLists.txt
│   │   └── sdkconfig.defaults
│   │
│   └── node_c_sentinel/               # IMU door/window sentinel
│       ├── main/
│       │   ├── main.c
│       │   ├── imu_driver.c
│       │   ├── event_buffer.c         # Circular buffer for vibration events
│       │   └── mcp_tools.c
│       ├── CMakeLists.txt
│       └── sdkconfig.defaults
│
├── agent/
│   ├── __init__.py
│   ├── main.py                        # Entry point — starts agent loop
│   ├── mcp_client.py                  # MCP-over-MQTT client wrapper
│   ├── spatial_agent.py               # LlamaIndex agent with spatial prompt
│   ├── geojson_builder.py             # Converts agent output to GeoJSON
│   ├── comfort_thresholds.py          # IAQ threshold definitions
│   ├── config.py                      # Broker URL, API keys, coordinates
│   └── prompts/
│       └── spatial_analyst.txt        # System prompt for the LLM
│
├── visualization/
│   ├── qgis/
│   │   ├── floor_plan.qgz             # QGIS project with floor plan basemap
│   │   └── styles/                    # Layer styles for sensor data
│   └── leaflet/
│       └── dashboard.html             # Web-based live dashboard
│
├── spatial/
│   ├── coordinate_system.json         # Building coordinate definitions
│   ├── sensor_placements.json         # Node locations and metadata
│   └── room_definitions.json          # Room boundaries as polygons
│
└── tests/
    ├── test_mcp_discovery.py          # Verify tool discovery
    ├── test_agent_queries.py          # Test reasoning chains
    ├── test_geojson_output.py         # Validate GeoJSON structure
    └── mock_sensor_data.json          # Mock data for testing without hardware
```

---

## References

See `docs/references.md` for the full annotated bibliography (40+ sources). Key references:

**MCP + IoT:**
- Yang, N. et al. (2025). "IoT-MCP: Bridging LLMs and IoT Systems Through Model Context Protocol." ACM WiNTECH '25. [arXiv:2510.01260](https://arxiv.org/abs/2510.01260)
- EMQX (2025). "Building Your AI Companion with ESP32 & MCP over MQTT" (6-part series). [Tutorial](https://www.emqx.com/en/blog/esp32-and-mcp-over-mqtt)

**Agentic IoT:**
- Elewah, A. et al. (2025). "Agentic Search Engine for Real-Time Internet of Things Data." Sensors 25(19). [DOI:10.3390/s25195995](https://doi.org/10.3390/s25195995)

**Edge LLM:**
- Picovoice. picoLLM Inference Engine. [GitHub](https://github.com/Picovoice/picollm)
- RightNow-AI (2026). PicoLM. [GitHub](https://github.com/RightNow-AI/picolm)

**Indoor Air Quality + ESP32:**
- Sensirion SEN66 + ESP32-S3. [GitHub](https://github.com/mike-rankin/ESP32-S3_SEN66_Sensor)
- MDPI Buildings (2024). "Automatic Indoor Thermal Comfort Monitoring Based on BIM and IoT." [DOI:10.3390/buildings14113361](https://www.mdpi.com/2075-5309/14/11/3361)

**Spatial AI:**
- Niantic Spatial (2025). "Geospatial AI: A Guide to Spatial Intelligence."
- ScienceDirect (2024). "Employing Collective Intelligence at the IoT Edge for Spatial Decisions."

---

## License

MIT

---

## Contributing

This is a research/learning project. Contributions welcome via pull request.
