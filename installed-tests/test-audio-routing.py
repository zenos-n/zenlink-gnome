#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run with the default phone streams active; no audio is recorded."""

import json
import subprocess
import sys


def objects(kind):
    return json.loads(subprocess.check_output(["pactl", "--format=json", "list", kind]))


sinks = objects("sinks")
sources = objects("sources")
inputs = objects("sink-inputs")
outputs = objects("source-outputs")
playback = [sink for sink in sinks if sink["name"].startswith("zenlink_output_")]
if len(sys.argv) > 1:
    playback = [sink for sink in playback if sink["name"] == "zenlink_output_" + sys.argv[1]]
assert playback, "Start the default streams from the phone first"

for sink in playback:
    suffix = sink["name"].removeprefix("zenlink_output_")
    feed = next(item for item in sinks if item["name"] == "zenlink_mic_feed_" + suffix)
    microphone = next(item for item in sources if item["name"] == "zenlink_microphone_" + suffix)
    monitor = next(item for item in sources if item["name"] == sink["name"] + ".monitor")
    feed_monitor = next(item for item in sources if item["name"] == feed["name"] + ".monitor")
    assert feed["index"] != sink["index"], "Microphone and playback use the same sink"
    assert str(feed["properties"].get("node.hidden")).lower() == "true", "Microphone feed is exposed as a playback device"
    assert microphone["name"] != monitor["name"], "Microphone is the playback monitor"
    is_gstreamer = lambda item: "gst-launch" in item.get("properties", {}).get("application.process.binary", "")
    assert any(is_gstreamer(item) and item["sink"] == feed["index"] for item in inputs), "Phone microphone receiver is missing"
    assert any(is_gstreamer(item) and item["source"] == monitor["index"] for item in outputs), "PC playback capture is missing"
    assert not any(is_gstreamer(item) and item["source"] == feed_monitor["index"] for item in outputs), "Microphone is being sent back as playback"

print("Live audio routing passed: microphone input and phone playback are isolated")
