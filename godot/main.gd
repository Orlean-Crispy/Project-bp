extends Node3D
# Presentation only: this script reads input, drives the simulation through the
# GDExtension bridge, and positions the camera. It never computes movement.

const TICK := 1.0 / 64.0
const SENSITIVITY := 0.0022
const EYE_FALLBACK := 1.55

# SimEvent::type -> display name (plan 3.4). M6 adds muzzle/impact/etc.
const EVENT_NAMES := { 1: "jump", 2: "land" }

# sim key name -> Godot key constant
const KEYCODES := {
	"A": KEY_A, "B": KEY_B, "C": KEY_C, "D": KEY_D, "E": KEY_E, "F": KEY_F,
	"G": KEY_G, "H": KEY_H, "I": KEY_I, "J": KEY_J, "K": KEY_K, "L": KEY_L,
	"M": KEY_M, "N": KEY_N, "O": KEY_O, "P": KEY_P, "Q": KEY_Q, "R": KEY_R,
	"S": KEY_S, "T": KEY_T, "U": KEY_U, "V": KEY_V, "W": KEY_W, "X": KEY_X,
	"Y": KEY_Y, "Z": KEY_Z,
	"SPACE": KEY_SPACE, "LEFT_CTRL": KEY_CTRL, "LEFT_SHIFT": KEY_SHIFT,
	"RIGHT_CTRL": KEY_CTRL, "RIGHT_SHIFT": KEY_SHIFT, "TAB": KEY_TAB,
	"ESCAPE": KEY_ESCAPE, "MOUSE_LEFT": KEY_NONE,
}

var sim
var camera: Camera3D
var hud: Label
var yaw := 0.0
var pitch := 0.0
var accumulator := 0.0
var mouse_captured := true
var autoplay := false
var last_event := "-"

func _ready() -> void:
	RenderingServer.set_default_clear_color(Color(0.07, 0.08, 0.1))
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
	autoplay = OS.get_cmdline_args().has("--autoplay")

	sim = SimWorld.new()
	var base := ProjectSettings.globalize_path("res://")
	var map_path := base.path_join("../sim/data/maps/playground.map")
	var data_path := base.path_join("../sim/data/movement.toml")
	if not sim.load_map(map_path, data_path):
		push_error("cannot load playground map")
		return
	if not sim.load_bindings(base.path_join("../sim/data/keybinds.toml")):
		push_warning("cannot load keybinds, using defaults")
	_build_actions(sim.get_bindings())
	_build_world_mesh()

	camera = Camera3D.new()
	camera.fov = 90.0
	camera.near = 0.05
	camera.far = 200.0
	add_child(camera)
	camera.current = true

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -35, 0)
	light.light_energy = 0.9
	add_child(light)

	hud = Label.new()
	hud.position = Vector2(16, 12)
	hud.add_theme_font_size_override("font_size", 16)
	hud.add_theme_color_override("font_color", Color(0.9, 0.92, 0.95))
	add_child(hud)

func _build_actions(bindings: Dictionary) -> void:
	for action_name in bindings.keys():
		var godot_action := "bp_" + String(action_name)
		if not InputMap.has_action(godot_action):
			InputMap.add_action(godot_action)
		var key_name := String(bindings[action_name])
		if not KEYCODES.has(key_name):
			continue
		var code: Key = KEYCODES[key_name]
		if code == KEY_NONE:
			continue
		var event := InputEventKey.new()
		event.physical_keycode = code
		InputMap.action_add_event(godot_action, event)

func _build_world_mesh() -> void:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = sim.get_vertices()
	arrays[Mesh.ARRAY_NORMAL] = sim.get_normals()
	arrays[Mesh.ARRAY_COLOR] = sim.get_colors()
	arrays[Mesh.ARRAY_INDEX] = sim.get_indices()

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var material := StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.cull_mode = BaseMaterial3D.CULL_DISABLED

	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	add_child(instance)

func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and mouse_captured:
		# sim yaw grows counter-clockwise (left); moving the mouse right must
		# decrease it, otherwise the whole view feels mirrored.
		yaw -= event.relative.x * SENSITIVITY
		pitch = clampf(pitch - event.relative.y * SENSITIVITY, -1.5533, 1.5533)
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		mouse_captured = not mouse_captured
		Input.set_mouse_mode(
			Input.MOUSE_MODE_CAPTURED if mouse_captured else Input.MOUSE_MODE_VISIBLE
		)

func _pressed(action: String) -> bool:
	return Input.is_action_pressed("bp_" + action)

func _process(delta: float) -> void:
	if sim == null or camera == null:
		return

	var forward := Input.get_action_strength("bp_MoveForward") - Input.get_action_strength("bp_MoveBack")
	var right := Input.get_action_strength("bp_MoveRight") - Input.get_action_strength("bp_MoveLeft")

	# --autoplay: hold forward for two seconds. Used by the smoke check to prove
	# that input -> simulation -> camera actually works without a human.
	# Drive autoplay from simulation ticks, not frames: the frame rate must not
	# change what the demo does (and headless runs at a different rate).
	var tick := int(sim.get_tick())
	var jump := _pressed("Jump")
	if autoplay and tick < 128:
		# Stand still until grounded, jump once, then move. This makes the
		# event-channel check deterministic instead of depending on the spawn.
		jump = jump or tick == 20
		if tick >= 40:
			if tick < 90:
				forward = 1.0
			else:
				right = 1.0

	sim.set_input(
		forward, right, yaw,
		jump, _pressed("Crouch"), _pressed("Slide"), _pressed("Dash"),
		_pressed("WallRun"), _pressed("WallClimb"), _pressed("Vault")
	)

	accumulator += delta
	var ticks := 0
	while accumulator >= TICK and ticks < 5:
		sim.step()
		accumulator -= TICK
		ticks += 1
	if ticks == 5:
		accumulator = 0.0

	var events: Array = sim.drain_events()
	for event in events:
		last_event = "%s @%d" % [
			EVENT_NAMES.get(int(event["type"]), "?"), int(event["tick"])
		]
		if autoplay:
			print("event: ", last_event)

	var position: Vector3 = sim.get_position()
	camera.position = position + Vector3(0.0, sim.get_eye_height(), 0.0)
	# sim yaw 0 looks down +X and maps to Godot (x, z, -y)
	var godot_yaw := atan2(-cos(yaw), sin(yaw))
	camera.rotation = Vector3(pitch, godot_yaw, 0.0)

	var velocity: Vector3 = sim.get_velocity()
	var speed := Vector2(velocity.x, velocity.z).length()
	var states := ["ground", "air", "slide", "wallrun", "wallclimb", "vault"]
	hud.text = "speed %5.2f m/s   %s   impulses %d\npos %.1f %.1f %.1f\nlast event %s" % [
		speed, states[sim.get_state()], sim.get_impulses(),
		position.x, position.y, position.z, last_event
	]
