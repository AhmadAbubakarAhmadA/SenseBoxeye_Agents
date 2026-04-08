"""Spatial reasoning agent using Anthropic Claude with tool_use."""

import json
from pathlib import Path

import anthropic

from agent.config import ANTHROPIC_API_KEY, LLM_MODEL
from agent.mqtt_reader import MQTTReader
from agent.tools import TOOL_DEFINITIONS, handle_tool
from agent.geojson_builder import snapshot_geojson

_SYSTEM_PROMPT = (Path(__file__).resolve().parent / "prompts" / "spatial_analyst.txt").read_text()


class SpatialAgent:
    def __init__(self, reader: MQTTReader):
        self.reader = reader
        self.client = anthropic.Anthropic(api_key=ANTHROPIC_API_KEY)
        self.model = LLM_MODEL
        self.messages: list[dict] = []

    def reset(self):
        self.messages = []

    def query(self, user_text: str) -> str:
        """Send a user query, handle tool calls, return the final text response."""
        self.messages.append({"role": "user", "content": user_text})

        # Agentic loop: keep going while Claude wants to call tools
        while True:
            response = self.client.messages.create(
                model=self.model,
                max_tokens=2048,
                system=_SYSTEM_PROMPT,
                tools=TOOL_DEFINITIONS,
                messages=self.messages,
            )

            # Collect assistant content blocks
            assistant_content = response.content
            self.messages.append({"role": "assistant", "content": assistant_content})

            if response.stop_reason != "tool_use":
                # Extract text blocks from the response
                text_parts = [b.text for b in assistant_content if b.type == "text"]
                return "\n".join(text_parts)

            # Handle tool calls
            tool_results = []
            for block in assistant_content:
                if block.type != "tool_use":
                    continue
                result = handle_tool(block.name, block.input, self.reader)
                tool_results.append({
                    "type": "tool_result",
                    "tool_use_id": block.id,
                    "content": result,
                })

            self.messages.append({"role": "user", "content": tool_results})

    def geojson_snapshot(self) -> str:
        """Return a GeoJSON snapshot of all current readings."""
        return snapshot_geojson(self.reader.get_all_latest())
