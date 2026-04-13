# SPATIAL-SENSE-AGENT 

AUTONOMOUS SPATIAL REASONING FOR IOT SENSOR NETWORKS VIA LLM-DRIVEN MCP DISCOVER.

An LLM agent that discovers IoT sensors over MQTT at runtime, calls them as tools, and reasons about the indoor space they're deployed in. Each ESP32 sensor node publishes a tool manifest (MCP-style) to the MQTT broker. The agent picks these up, wires them into LlamaIndex as `FunctionTool`s, and uses Claude's native tool_use to query real sensor data on demand. Output is natural language spatial analysis + GeoJSON.

## How it works

There are three layers:

1. **Sensor nodes** (ESP32-S3, ESP-IDF C firmware). Each node reads its sensor(s), connects to Wi-Fi, and publishes two things to MQTT: (a) a retained JSON tool manifest on `sensors/<id>/tools` describing what it can do, and (b) periodic data on `sensors/<id>/data`. Currently three nodes are live:
   - **Node A** — SEN66 air quality sensor (CO2, temperature, humidity, VOC, NOx, PM) + ICM-20948 IMU, at the center of Room
   - **Node B** —  OV2640 camera + ICM-20948 IMU at the NW corner of Room, aimed at the entry for visual scene observation
   - **Node C** — ICM-20948 IMU on the door frame, detecting open/close via vibration spikes

3. **MQTT broker** (EMQX, Docker). Receives everything. Retained messages mean the agent gets tool manifests immediately on connect, even if the nodes booted hours ago.

4. **Python agent** (LlamaIndex + Anthropic Claude). `MQTTReader` subscribes in the background. On startup, `mcp_bridge.py` converts discovered tool manifests into LlamaIndex `FunctionTool` objects. `AgentWorkflow` handles the agentic loop — Claude decides which sensors to query, interprets the data, correlates across nodes, and responds.

The key idea: spatial metadata (x, y, z coordinates, room, floor) is embedded directly in tool descriptions and every MQTT message. The LLM doesn't need hardcoded building knowledge — it discovers sensor locations through the same protocol it uses to discover sensor capabilities. Add a new ESP32 node, and the agent finds it automatically.

## Files

```
agent/
  main.py              # entry point, interactive REPL
  spatial_agent.py      # LlamaIndex AgentWorkflow + Anthropic LLM
  mcp_bridge.py         # MQTT manifests → LlamaIndex FunctionTools
  tools.py              # tool schemas + handlers (local fallback)
  mqtt_reader.py        # background MQTT subscriber, caches readings + manifests
  geojson_builder.py    # readings → GeoJSON FeatureCollection
  config.py             # .env loader
  prompts/
    spatial_analyst.txt  # system prompt

firmware/
  node_a_environment/   # ESP-IDF: SEN66 + IMU → MQTT (air quality station)
  node_c_sentinel/      # ESP-IDF: IMU → vibration events → MQTT (door sentinel)

spatial/
  sensor_placements.json # node coordinates, rooms, coordinate system
```

The firmware builds on Windows with ESP-IDF v6.0. Source is mirrored here for version control. `sdkconfig.local` (Wi-Fi/MQTT credentials) is gitignored.

## Setup

Requires Python 3.12+, an EMQX broker, and an Anthropic API key.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Create `.env`:

```
MQTT_HOST=10.130.71.4
MQTT_PORT=1883
MQTT_USER=esp32_sensor
MQTT_PASS=<password>
ANTHROPIC_API_KEY=sk-ant-api03-...
```

## Running

```bash
python -m agent.main
```

The agent connects to MQTT, discovers tool manifests from any online sensor nodes, and drops into a REPL. Ask it about the environment:

```
You> What's the air quality like?
You> Is the door open?
You> Has anyone entered in the last 10 minutes?
You> /geojson
You> /quit
```

## Hardware

All nodes use the [senseBox Eye v1.4](https://sensebox.de/) — ESP32-S3-WROOM-1, 8 MB flash, OctalSPI PSRAM, QWIIC I2C on GPIO2/GPIO1, onboard ICM-20948 IMU. Two of the four boards have OV2640 cameras.

| Node | Sensor ID | What it does | Position |
|------|-----------|-------------|----------|
| A | env_station_01 | CO2, T, RH, VOC, NOx, PM, motion | (6.0, 6.0, 1.2) Lab B center |
| C | door_sentinel_01 | Door vibration events | (9.5, 4.0, 2.1) Door frame |
| B | visual_observer_01 | Camera + IMU (planned) | (2.5, 8.5, 2.0) Lab B NW corner |
| D | hallway_observer_01 | Camera + IMU (planned) | (12.0, 1.0, 2.0) Hallway |

Coordinates are local Cartesian meters, origin at SW corner of the building. X = East, Y = North, Z = height.

## Design doc


