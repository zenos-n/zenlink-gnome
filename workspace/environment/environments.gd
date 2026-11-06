extends Node3D

var environment: Environment
var scenery: Node3D
var custom: Node3D
var xr: OpenXRInterface
var mode := "passthrough"

func setup(interface: OpenXRInterface) -> void:
	xr = interface
	var world := WorldEnvironment.new()
	environment = Environment.new()
	world.environment = environment
	add_child(world)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("d9d8df")
	environment.ambient_light_energy = 0.7
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-32, -22, 0)
	sun.light_color = Color("ffe3c6")
	sun.light_energy = 1.0
	add_child(sun)
	scenery = Node3D.new()
	add_child(scenery)
	build_atrium()
	set_mode("passthrough" if xr and xr.is_initialized() else "atrium")

func material(color: Color) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = 0.85
	return mat

func block(size: Vector3, position_value: Vector3, color: Color) -> void:
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = size
	mesh.mesh = box
	mesh.material_override = material(color)
	scenery.add_child(mesh)
	mesh.position = position_value

func build_atrium() -> void:
	block(Vector3(22, 0.12, 22), Vector3(0, -0.08, 0), Color("363a3f"))
	for i in range(-5, 6):
		block(Vector3(0.012, 0.008, 22), Vector3(i * 2, 0, 0), Color("4b5058"))
		block(Vector3(22, 0.008, 0.012), Vector3(0, 0, i * 2), Color("4b5058"))
	for i in range(12):
		var angle := float(i) * TAU / 12.0
		var point := Vector3(sin(angle) * 8.0, 0, cos(angle) * 8.0)
		block(Vector3(0.35, 4.8, 0.35), point + Vector3(0, 2.4, 0), Color("c3b8ab"))
		var mesh := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 1.0
		sphere.height = 2.0
		mesh.mesh = sphere
		mesh.material_override = material(Color("476650"))
		scenery.add_child(mesh)
		mesh.position = point * 1.2 + Vector3(0, 1.2, 0)
	block(Vector3(4, 0.35, 0.8), Vector3(-5, 0.5, -4), Color("78624f"))
	block(Vector3(4, 0.35, 0.8), Vector3(5, 0.5, -4), Color("78624f"))

func set_mode(value: String) -> bool:
	var live := xr and xr.is_initialized()
	var passthrough := value == "passthrough" and live and XRInterface.XR_ENV_BLEND_MODE_ALPHA_BLEND in xr.get_supported_environment_blend_modes()
	if value == "passthrough" and live and not passthrough:
		return false
	if live:
		xr.environment_blend_mode = XRInterface.XR_ENV_BLEND_MODE_ALPHA_BLEND if passthrough else XRInterface.XR_ENV_BLEND_MODE_OPAQUE
	get_viewport().transparent_bg = passthrough
	scenery.visible = value == "atrium"
	if custom:
		custom.visible = value == "custom3d"
	if passthrough:
		environment.background_mode = Environment.BG_COLOR
		# OpenXR uses premultiplied alpha; transparent white becomes a white veil.
		environment.background_color = Color(0, 0, 0, 0)
	else:
		var sky := Sky.new()
		var panorama_path := ""
		if value == "sunset": panorama_path = "res://assets/venice_sunset_1k.hdr"
		if value == "studio": panorama_path = "res://assets/studio_small_09_1k.hdr"
		if value == "customhdri": panorama_path = "user://environment.hdr"
		if not panorama_path.is_empty():
			var texture: Texture2D
			if panorama_path.begins_with("res://"):
				texture = load(panorama_path)
			else:
				var image := Image.load_from_file(panorama_path)
				if not image: return false
				texture = ImageTexture.create_from_image(image)
			var panorama := PanoramaSkyMaterial.new()
			panorama.panorama = texture
			sky.sky_material = panorama
		else:
			var procedural := ProceduralSkyMaterial.new()
			procedural.sky_top_color = Color("586b88")
			procedural.sky_horizon_color = Color("d0b8a6")
			procedural.ground_bottom_color = Color("30343b")
			procedural.ground_horizon_color = Color("c0b0a0")
			sky.sky_material = procedural
		environment.background_mode = Environment.BG_SKY
		environment.sky = sky
	mode = value
	print("WORKSPACE_ENVIRONMENT mode=", mode, " local=true passthrough=", passthrough)
	return true

func load_custom_scene() -> bool:
	if not FileAccess.file_exists("user://environment.glb"):
		return false
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file("user://environment.glb", state) != OK:
		return false
	var scene := document.generate_scene(state)
	if not scene:
		return false
	if custom: custom.queue_free()
	custom = scene
	add_child(custom)
	return set_mode("custom3d")
