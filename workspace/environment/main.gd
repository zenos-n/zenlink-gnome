extends Node3D
func _ready() -> void:
	var xr: OpenXRInterface = XRServer.find_interface("OpenXR")
	if not "--self-test" in OS.get_cmdline_user_args() and xr and xr.initialize(): get_viewport().use_xr = true
	var origin := XROrigin3D.new()
	origin.add_child(XRCamera3D.new())
	add_child(origin)
	var scenery := preload("res://environments.gd").new()
	add_child(scenery)
	scenery.setup(xr)
	scenery.set_mode("atrium")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--environment="): scenery.set_mode(arg.trim_prefix("--environment="))
	if "--self-test" in OS.get_cmdline_user_args():
		assert(scenery.set_mode("sunset"))
		assert(scenery.set_mode("studio"))
		assert(scenery.set_mode("atrium"))
		print("ZENLINK_PC_ENVIRONMENT_PASS")
		get_tree().quit()
