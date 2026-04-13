"""Spatial reasoning agent using LlamaIndex with Anthropic Claude."""

import asyncio
from pathlib import Path

from llama_index.core.agent import AgentWorkflow
from llama_index.llms.anthropic import Anthropic

from agent.config import ANTHROPIC_API_KEY, LLM_MODEL
from agent.mqtt_reader import MQTTReader
from agent.mcp_bridge import build_tools_from_manifests
from agent.geojson_builder import snapshot_geojson

_SYSTEM_PROMPT = (Path(__file__).resolve().parent / "prompts" / "spatial_analyst.txt").read_text()


class SpatialAgent:
    """LlamaIndex-based spatial reasoning agent with MCP-over-MQTT tool discovery."""

    def __init__(self, reader: MQTTReader):
        self.reader = reader

        # Build LLM
        self.llm = Anthropic(
            model=LLM_MODEL,
            api_key=ANTHROPIC_API_KEY,
            max_tokens=4096,
            temperature=0.1,
        )

        # Discover tools from MQTT manifests → LlamaIndex FunctionTools
        self.tools = build_tools_from_manifests(reader)
        print(f"[agent] Loaded {len(self.tools)} tools into LlamaIndex agent.")

        # Build the agent workflow
        self.workflow = AgentWorkflow.from_tools_or_functions(
            tools_or_functions=self.tools,
            llm=self.llm,
            system_prompt=_SYSTEM_PROMPT,
            verbose=False,
        )

    def reset(self):
        """Reset conversation state by rebuilding the workflow."""
        self.workflow = AgentWorkflow.from_tools_or_functions(
            tools_or_functions=self.tools,
            llm=self.llm,
            system_prompt=_SYSTEM_PROMPT,
            verbose=False,
        )

    def query(self, user_text: str) -> str:
        """Send a user query through the LlamaIndex agent and return the text response."""
        result = asyncio.run(self.workflow.run(user_msg=user_text))
        return str(result.response)

    async def aquery(self, user_text: str) -> str:
        """Async version of query."""
        result = await self.workflow.run(user_msg=user_text)
        return str(result.response)

    def geojson_snapshot(self) -> str:
        """Return a GeoJSON snapshot of all current readings."""
        return snapshot_geojson(self.reader.get_all_latest())
