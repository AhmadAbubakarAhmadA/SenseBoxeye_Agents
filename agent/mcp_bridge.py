"""Bridge MQTT-discovered tool manifests to LlamaIndex FunctionTool objects."""

import json
from typing import Any

from llama_index.core.tools import FunctionTool

from agent.mqtt_reader import MQTTReader
from agent.tools import TOOL_DEFINITIONS, handle_tool


def _make_tool_fn(tool_name: str, reader: MQTTReader):
    """Create a callable that dispatches to handle_tool for a given tool name."""

    def _call(**kwargs: Any) -> str:
        return handle_tool(tool_name, kwargs, reader)

    _call.__name__ = tool_name
    return _call


def _build_fn_schema(input_schema: dict) -> dict[str, tuple[type, Any]]:
    """Convert Anthropic-style input_schema to (type, default) pairs for FunctionTool."""
    props = input_schema.get("properties", {})
    required = set(input_schema.get("required", []))
    schema = {}
    type_map = {"string": str, "integer": int, "number": float, "boolean": bool}
    for name, spec in props.items():
        py_type = type_map.get(spec.get("type", "string"), str)
        if name in required:
            schema[name] = (py_type, ...)
        else:
            schema[name] = (py_type, None)
    return schema


def build_tools_from_manifests(reader: MQTTReader) -> list[FunctionTool]:
    """Discover tool manifests from MQTT and create LlamaIndex FunctionTools.

    Falls back to local TOOL_DEFINITIONS if no manifests are available.
    """
    manifests = reader.get_manifests()
    tool_defs: list[dict] = []

    if manifests:
        for sid, manifest in manifests.items():
            tools = manifest.get("tools", [])
            print(
                f"[MCP bridge] Sensor {sid} advertised {len(tools)} tools: "
                f"{', '.join(t['name'] for t in tools)}"
            )
            tool_defs.extend(tools)
    else:
        print("[MCP bridge] No MQTT manifests — using local tool definitions.")
        tool_defs = TOOL_DEFINITIONS

    function_tools: list[FunctionTool] = []
    for tdef in tool_defs:
        name = tdef["name"]
        description = tdef.get("description", name)
        fn = _make_tool_fn(name, reader)
        fn.__doc__ = description

        # Build the FunctionTool with explicit schema from the manifest
        fn_schema = _build_fn_schema(
            tdef.get("input_schema", {"type": "object", "properties": {}})
        )

        tool = FunctionTool.from_defaults(
            fn=fn,
            name=name,
            description=description,
        )
        function_tools.append(tool)

    return function_tools
