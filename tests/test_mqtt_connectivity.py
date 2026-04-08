"""Phase 0 smoke test: pub/sub round-trip against the local EMQX broker."""
import time
import json
import paho.mqtt.client as mqtt

from agent.config import MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS

TOPIC = "sensors/test/hello"
received = []


def on_message(_c, _u, msg):
    received.append(json.loads(msg.payload))


def test_pubsub_roundtrip():
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="phase0_test")
    if MQTT_USER:
        c.username_pw_set(MQTT_USER, MQTT_PASS)
    c.on_message = on_message
    subscribed = []
    c.on_subscribe = lambda *a, **k: subscribed.append(True)
    c.connect(MQTT_HOST, MQTT_PORT, 30)
    c.loop_start()
    c.subscribe(TOPIC, qos=1)
    deadline = time.time() + 5
    while not subscribed and time.time() < deadline:
        time.sleep(0.05)

    payload = {"ts": time.time(), "msg": "hello from spatialsense"}
    c.publish(TOPIC, json.dumps(payload), qos=1).wait_for_publish(timeout=5)

    deadline = time.time() + 5
    while not received and time.time() < deadline:
        time.sleep(0.05)

    c.loop_stop()
    c.disconnect()

    assert received, "no message received from broker"
    assert received[0]["msg"] == "hello from spatialsense"


if __name__ == "__main__":
    test_pubsub_roundtrip()
    print("OK — broker round-trip works")
