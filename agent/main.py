"""Entry point for the SpatialSenseAgent interactive loop."""

import sys
import time

from agent.mqtt_reader import MQTTReader
from agent.spatial_agent import SpatialAgent


def main():
    print("SpatialSenseAgent starting (LlamaIndex + MCP-over-MQTT)...")

    # 1. Connect to MQTT and wait for first reading
    reader = MQTTReader()
    if not reader.start(timeout=10):
        print("ERROR: Could not connect to MQTT broker. Check .env settings.")
        sys.exit(1)
    print("MQTT connected. Waiting for first sensor reading...")

    for _ in range(30):  # wait up to 15s
        if reader.get_all_latest():
            break
        time.sleep(0.5)
    else:
        print("WARNING: No sensor data received yet. The agent will work once data arrives.")

    latest = reader.get_all_latest()
    if latest:
        sids = ", ".join(latest.keys())
        print(f"Receiving data from: {sids}")

    # Give retained tool manifests a moment to arrive
    for _ in range(10):
        if reader.get_manifests():
            break
        time.sleep(0.2)
    manifests = reader.get_manifests()
    if manifests:
        print(f"Discovered tool manifests from: {', '.join(manifests.keys())}")
    else:
        print("No tool manifests discovered — will use local fallback definitions.")
    print()

    # 2. Start LlamaIndex agent with discovered tools
    agent = SpatialAgent(reader)

    print("Ready. Ask questions about the indoor environment.")
    print("Commands:  /geojson  — print GeoJSON snapshot")
    print("           /reset    — clear conversation history")
    print("           /quit     — exit")
    print()

    while True:
        try:
            user_input = input("You> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not user_input:
            continue
        if user_input == "/quit":
            break
        if user_input == "/reset":
            agent.reset()
            print("Conversation reset.\n")
            continue
        if user_input == "/geojson":
            print(agent.geojson_snapshot())
            print()
            continue

        try:
            answer = agent.query(user_input)
            print(f"\nAgent> {answer}\n")
        except Exception as e:
            print(f"\nError: {e}\n")

    reader.stop()
    print("Goodbye.")


if __name__ == "__main__":
    main()
