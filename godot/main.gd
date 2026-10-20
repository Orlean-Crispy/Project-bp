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
var host_mode := false
var connect_ip := ""
var net_port := 27115  # A1 signalling; the game port is this + 1
var joined_printed := false
var last_peer_count := -1
var remote_nodes := {}
var net_menu: CanvasLayer
var menu_open := false
var force_menu := false

func _ready() -> void:
	RenderingServer.set_default_clear_color(Color(0.07, 0.08, 0.1))
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
	var cli := OS.get_cmdline_args()
	cli.append_array(OS.get_cmdline_user_args())
	autoplay = cli.has("--autoplay")
	for arg in cli:
		if arg == "--host":
			host_mode = true
		elif arg == "--connect":
			connect_ip = "127.0.0.1"
		elif arg.begins_with("--connect="):
			connect_ip = arg.substr(10)
		elif arg.begins_with("--port="):
			net_port = int(arg.substr(7))
		elif arg == "--menu":
			force_menu = true

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

	if host_mode:
		if sim.host(net_port, 2):
			print("breakpoint: hosting on port ", net_port)
		else:
			push_error("host failed")
	elif connect_ip != "":
		if sim.join(connect_ip, net_port):
			print("breakpoint: connecting to ", connect_ip, ":", net_port)
		else:
			push_error("join failed")
	elif force_menu or (not autoplay and DisplayServer.get_name() != "headless"):
		_show_net_menu()

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

func _close_net_menu() -> void:
	menu_open = false
	if net_menu != null:
		net_menu.queue_free()
		net_menu = null
	mouse_captured = true
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)

func _show_net_menu() -> void:
	menu_open = true
	mouse_captured = false
	Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	net_menu = CanvasLayer.new()
	add_child(net_menu)

	var panel := PanelContainer.new()
	panel.position = Vector2(40, 60)
	panel.custom_minimum_size = Vector2(360, 0)
	net_menu.add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	panel.add_child(box)

	var title := Label.new()
	title.text = "IMPULSE - LAN"
	title.add_theme_font_size_override("font_size", 20)
	box.add_child(title)
	var hint := Label.new()
	hint.text = "Signal port (UDP is port+1). Host here, or join the host IP."
	box.add_child(hint)

	var ip := LineEdit.new()
	ip.text = "127.0.0.1"
	ip.placeholder_text = "host IP"
	box.add_child(ip)
	var port := LineEdit.new()
	port.text = str(net_port)
	port.placeholder_text = "port"
	box.add_child(port)
	var status := Label.new()
	status.text = ""
	box.add_child(status)

	var host_button := Button.new()
	host_button.text = "Host"
	box.add_child(host_button)
	host_button.pressed.connect(func() -> void:
		if sim.host(int(port.text), 2):
			print("breakpoint: hosting on port ", port.text)
			_close_net_menu()
		else:
			status.text = "host failed (is the port in use?)"
	)

	var join_button := Button.new()
	join_button.text = "Join"
	box.add_child(join_button)
	join_button.pressed.connect(func() -> void:
		if sim.join(ip.text, int(port.text)):
			print("breakpoint: connecting to ", ip.text, ":", port.text)
			_close_net_menu()
		else:
			status.text = "join failed (check IP/port)"
	)

	var solo_button := Button.new()
	solo_button.text = "Single player"
	box.add_child(solo_button)
	solo_button.pressed.connect(func() -> void:
		_close_net_menu()
	)
	print("breakpoint: net menu open")

func _update_remote_players(players: Array) -> void:
	var alive := {}
	for entry in players:
		var slot := int(entry["slot"])
		alive[slot] = true
		var node: MeshInstance3D = remote_nodes.get(slot)
		if node == null:
			node = MeshInstance3D.new()
			var capsule := CapsuleMesh.new()
			capsule.radius = 0.405
			capsule.height = 1.42
			node.mesh = capsule
			var material := StandardMaterial3D.new()
			material.albedo_color = Color(0.95, 0.35, 0.25)
			material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			node.material_override = material
			var nose := MeshInstance3D.new()
			var nose_mesh := BoxMesh.new()
			nose_mesh.size = Vector3(0.14, 0.14, 0.5)
			nose.mesh = nose_mesh
			nose.position = Vector3(0.0, 0.9, -0.35)
			var nose_material := StandardMaterial3D.new()
			nose_material.albedo_color = Color(1.0, 0.9, 0.7)
			nose_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			nose.material_override = nose_material
			node.add_child(nose)
			add_child(node)
			remote_nodes[slot] = node
			print("breakpoint: remote slot ", slot, " visible")
		var feet: Vector3 = entry["pos"]
		var crouched: bool = entry["crouched"]
		var height := 0.91 if crouched else 1.42
		node.scale = Vector3(1.0, height / 1.42, 1.0)
		node.position = feet + Vector3(0.0, height * 0.5, 0.0)
		var yaw: float = entry["yaw"]
		node.rotation.y = atan2(-cos(yaw), sin(yaw))
		if autoplay and abs(yaw - node.get_meta("logged_yaw", -99.0)) > 0.05:
			node.set_meta("logged_yaw", yaw)
			print("breakpoint: remote slot ", slot, " yaw ", yaw)
	for slot in remote_nodes.keys():
		if not alive.has(slot):
			remote_nodes[slot].queue_free()
			remote_nodes.erase(slot)

func _process(delta: float) -> void:
	if sim == null or camera == null:
		return
	if menu_open:
		accumulator = 0.0
		return

	var forward := Input.get_action_strength("bp_MoveForward") - Input.get_action_strength("bp_MoveBack")
	var right := Input.get_action_strength("bp_MoveRight") - Input.get_action_strength("bp_MoveLeft")

	# --autoplay: hold forward for two seconds. Used by the smoke check to prove
	# that input -> simulation -> camera actually works without a human.
	# Drive autoplay from simulation ticks, not frames: the frame rate must not
	# change what the demo does (and headless runs at a different rate).
	var tick := int(sim.get_tick())
	var jump := _pressed("Jump")
	if autoplay:
		yaw = 0.9  # make remote orientation observable in the headless check
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
		if sim.is_networked():
			sim.step_network()
		else:
			sim.step()
		accumulator -= TICK
		ticks += 1
	if ticks == 5:
		accumulator = 0.0

	if sim.is_networked():
		if sim.get_joined() and not joined_printed:
			joined_printed = true
			print("breakpoint: joined slot ", sim.get_player_slot())
		if sim.get_peer_count() != last_peer_count:
			last_peer_count = sim.get_peer_count()
			print("breakpoint: peers ", last_peer_count)
		_update_remote_players(sim.get_remote_players())

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
	var net_line := ""
	if sim.is_networked():
		net_line = "\nnet %s slot %d peers %d%s" % [
			sim.get_role(), sim.get_player_slot(), sim.get_peer_count(),
			"" if sim.get_joined() else " (connecting)"
		]
	hud.text = "speed %5.2f m/s   %s   impulses %d\npos %.1f %.1f %.1f\nlast event %s%s" % [
		speed, states[sim.get_state()], sim.get_impulses(),
		position.x, position.y, position.z, last_event, net_line
	]
