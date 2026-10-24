extends Node3D
# Presentation only: this script reads input, drives the simulation through the
# GDExtension bridge, and positions the camera. It never computes movement.

const TICK := 1.0 / 64.0
const SENSITIVITY := 0.0022

# SimEvent::type -> display name (plan 3.4). M6 adds muzzle/impact/etc.
const EVENT_NAMES := { 1: "jump", 2: "land", 3: "shot" }
# Must match sim::EventType::Shot.
const EVENT_SHOT := 3
# A tracer lives for this long; long enough to see, short enough to not smoke up a fight.
const TRACER_SECONDS := 0.09

# sim key name -> Godot key constant
const KEYCODES := {
	"A": KEY_A, "B": KEY_B, "C": KEY_C, "D": KEY_D, "E": KEY_E, "F": KEY_F,
	"G": KEY_G, "H": KEY_H, "I": KEY_I, "J": KEY_J, "K": KEY_K, "L": KEY_L,
	"M": KEY_M, "N": KEY_N, "O": KEY_O, "P": KEY_P, "Q": KEY_Q, "R": KEY_R,
	"S": KEY_S, "T": KEY_T, "U": KEY_U, "V": KEY_V, "W": KEY_W, "X": KEY_X,
	"Y": KEY_Y, "Z": KEY_Z,
	# Number row, not the numpad: sim::Key::Num0 is what key_name() prints.
	"NUM0": KEY_0, "NUM1": KEY_1, "NUM2": KEY_2, "NUM3": KEY_3, "NUM4": KEY_4,
	"NUM5": KEY_5, "NUM6": KEY_6, "NUM7": KEY_7, "NUM8": KEY_8, "NUM9": KEY_9,
	"SPACE": KEY_SPACE, "LEFT_CTRL": KEY_CTRL, "LEFT_SHIFT": KEY_SHIFT,
	"RIGHT_CTRL": KEY_CTRL, "RIGHT_SHIFT": KEY_SHIFT, "TAB": KEY_TAB,
	"ESCAPE": KEY_ESCAPE, "ENTER": KEY_ENTER, "BACKSPACE": KEY_BACKSPACE,
	"LEFT_ALT": KEY_ALT, "RIGHT_ALT": KEY_ALT,
	"UP": KEY_UP, "DOWN": KEY_DOWN, "LEFT": KEY_LEFT, "RIGHT": KEY_RIGHT,
}

# key_name() also emits mouse buttons (sim/input/keycodes.cpp).
const MOUSECODES := {
	"MOUSE_LEFT": MOUSE_BUTTON_LEFT,
	"MOUSE_RIGHT": MOUSE_BUTTON_RIGHT,
	"MOUSE_MIDDLE": MOUSE_BUTTON_MIDDLE,
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
var net_port := 0  # A1 signalling; 0 = bridge default, the game port is this + 1
var joined_printed := false
var last_peer_count := -1
var remote_nodes := {}
var net_menu: CanvasLayer
var menu_open := false
var force_menu := false
var status_label: Label
var game_menu: CanvasLayer
var game_menu_open := false
var hud_check := false
var silent_printed := false
var reconnect_check := false
var reconnect_done := false
var reconnect_locked := false
var force_paused := false
var autoplay_jumped := false
var menu_accumulator := 0.0
# M6: ["bp_Jump", bit] pairs and name -> bit, both filled from the bridge once.
var action_bits: Array = []
var button_bits := {}
var interp_check := false
var interp_done := false
# AUD-20: whole ticks thrown away when one frame is longer than the 5-tick cap.
# They are not hidden: the network client re-anchors its axis from the next
# snapshot, and this counter is what a gate can look at.
var dropped_ticks := 0
var dropped_reported := false
# M6.8 gate: host damages a slot, client prints hud lines until hp changes.
var damage_check := false
var damage_done := false
# M6.11 gate: hold the trigger offline and count the tracers that come back.
var fire_check := false
var fire_reported := false
var shot_events := 0
var hud_damage_check := false
# M6.11: live tracers (node + remaining life in seconds).
var tracers: Array = []

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
		elif arg == "--hud-check":
			hud_check = true
		elif arg == "--reconnect-check":
			reconnect_check = true
		elif arg == "--force-paused":
			# AUD-02 gate: start with the in-game menu open so a headless run can
			# prove the network pump keeps going while the world is paused.
			force_paused = true
		elif arg == "--interp-check":
			# Render-interpolation gate: proves the bridge exposes the previous tick,
			# and that it really is a different position while the player moves.
			interp_check = true
		elif arg == "--damage-check":
			# Host side of the M6.8 gate: hurt the joining slot once it is here.
			damage_check = true
		elif arg == "--hud-damage-check":
			# Client side: keep printing hud lines so the script can wait for hp=70.
			hud_damage_check = true
		elif arg == "--fire-check":
			# Offline gate: hold the trigger and count tracer events.
			fire_check = true

	sim = SimWorld.new()
	if net_port <= 0:
		# AUD-14: the default port lives in sim/net/ports.h, not in this script.
		net_port = int(sim.get_default_signal_port())
	var base := ProjectSettings.globalize_path("res://")
	var map_path := base.path_join("../sim/data/maps/playground.map")
	var data_path := base.path_join("../sim/data/movement.toml")
	if not sim.load_map(map_path, data_path):
		push_error("cannot load playground map")
		return
	if not sim.load_bindings(base.path_join("../sim/data/keybinds.toml")):
		push_warning("cannot load keybinds, using defaults")
	_build_actions(sim.get_bindings())
	_build_action_bits()
	_build_world_mesh()
	# AUD-13: the bridge is compiled with the same build id as headless_sim, so a
	# log line says which commit the loaded DLL really is (and which protocol).
	print("breakpoint: build %s protocol %d" % [sim.get_build_id(), sim.get_protocol_version()])

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

	status_label = Label.new()
	status_label.position = Vector2(16, 36)
	status_label.add_theme_font_size_override("font_size", 14)
	status_label.add_theme_color_override("font_color", Color(0.55, 0.72, 0.9))
	add_child(status_label)
	_update_hud()
	if hud_check:
		print("hud hp=%d armor=%d impulse=%d/%d role=%s vertices=%d build=%s protocol=%d port=%d" % [
			sim.get_hp(), sim.get_armor(), sim.get_impulses(), sim.get_max_impulses(),
			sim.get_role(), sim.get_vertices().size(),
			sim.get_build_id(), sim.get_protocol_version(), net_port
		])
		get_tree().quit()
	if force_paused:
		# AUD-02: start with the in-game menu open so a headless run can prove the
		# network pump keeps going while the world is paused.
		_toggle_game_menu()

func _update_hud() -> void:
	if hud == null:
		return
	var parts: Array[String] = []
	parts.append(str(sim.get_role()))
	if sim.is_networked():
		parts.append("slot %d" % sim.get_player_slot())
		parts.append("peers %d" % sim.get_peer_count())
		if not sim.get_joined():
			parts.append("waiting for server")
		elif sim.get_role() != "host":
			# Snapshot age is the only honest "server is gone" signal: the local
			# world keeps stepping even when nothing arrives.
			var age := int(sim.get_snapshot_age())
			if age > 64:
				parts.append("SERVER SILENT %.1fs" % (float(age) / 64.0))
				if not silent_printed:
					silent_printed = true
					print("breakpoint: server silent age=%d" % age)
			else:
				silent_printed = false
	status_label.text = "  ".join(parts)

# M6: the sim takes one button bitmask instead of a positional bool per action.
# The bits come from the bridge (sim/input/buttons.h), so this script never keeps
# its own copy of them; adding an action is a keybinds.toml + sim change only.
func _build_action_bits() -> void:
	var bits: Dictionary = sim.get_action_bits()
	for action in bits.keys():
		action_bits.append(["bp_" + String(action), int(bits[action])])
		button_bits[String(action)] = int(bits[action])

func _held_buttons() -> int:
	var mask := 0
	for pair in action_bits:
		if Input.is_action_pressed(pair[0]):
			mask |= int(pair[1])
	return mask

func _build_actions(bindings: Dictionary) -> void:
	for action_name in bindings.keys():
		var godot_action := "bp_" + String(action_name)
		if not InputMap.has_action(godot_action):
			InputMap.add_action(godot_action)
		var key_name := String(bindings[action_name])
		if key_name == "NONE":
			continue  # deliberately unbound
		# AUD-10: every name key_name() can print must map here, or a keybind edit
		# silently loses the action. Unknown names are loud now.
		if MOUSECODES.has(key_name):
			var button: MouseButton = MOUSECODES[key_name]
			var mouse_event := InputEventMouseButton.new()
			mouse_event.button_index = button
			InputMap.action_add_event(godot_action, mouse_event)
		elif KEYCODES.has(key_name):
			var code: Key = KEYCODES[key_name]
			var event := InputEventKey.new()
			event.physical_keycode = code
			InputMap.action_add_event(godot_action, event)
		else:
			push_warning("unknown key '%s' for action %s; the action is unbound" % [
				key_name, action_name
			])

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
	elif event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_ESCAPE:
		if menu_open:
			_close_net_menu()
		else:
			_toggle_game_menu()

func _toggle_game_menu() -> void:
	game_menu_open = not game_menu_open
	if game_menu_open:
		reconnect_locked = false  # a fresh menu is a fresh reconnect attempt
		mouse_captured = false
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
		game_menu = CanvasLayer.new()
		add_child(game_menu)
		var panel := PanelContainer.new()
		panel.position = Vector2(40, 60)
		panel.custom_minimum_size = Vector2(280, 0)
		game_menu.add_child(panel)
		var box := VBoxContainer.new()
		panel.add_child(box)
		var title := Label.new()
		title.text = "PAUSED"
		title.add_theme_font_size_override("font_size", 18)
		box.add_child(title)
		var resume := Button.new()
		resume.text = "Resume"
		resume.pressed.connect(_toggle_game_menu)
		box.add_child(resume)
		if sim != null and sim.is_networked() and sim.get_role() != "host":
			var again := Button.new()
			again.text = "Reconnect"
			# AUD-17: the button is only freed at the end of the frame, so a double
			# click used to run two joins back to back; _do_reconnect locks it.
			again.pressed.connect(func() -> void:
				if _do_reconnect():
					_toggle_game_menu()
			)
			box.add_child(again)
		var leave := Button.new()
		leave.text = "Leave"
		leave.pressed.connect(func() -> void: get_tree().quit())
		box.add_child(leave)
	else:
		if game_menu != null:
			game_menu.queue_free()
			game_menu = null
		if not menu_open:
			mouse_captured = true
			Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)

func _do_reconnect() -> bool:
	# AUD-17: exactly one reconnect per menu. A successful call retires the
	# ticket and replaces the link, so a second call in the same frame would
	# stack another join on top of it. A failed call is safe to retry.
	if reconnect_locked or sim == null or not sim.is_networked() or sim.get_role() == "host":
		return false
	reconnect_locked = true
	# A reconnect is a new join, so the one-shot log lines have to fire again.
	joined_printed = false
	silent_printed = false
	last_peer_count = -1
	var ok: bool = sim.reconnect()
	print("breakpoint: reconnect %s" % ("ok" if ok else "failed"))
	if not ok:
		reconnect_locked = false
	return ok

func _update_net_state() -> void:
	if not sim.is_networked():
		return
	if sim.get_joined() and not joined_printed:
		joined_printed = true
		print("breakpoint: joined slot ", sim.get_player_slot())
	if sim.get_peer_count() != last_peer_count:
		last_peer_count = sim.get_peer_count()
		print("breakpoint: peers ", last_peer_count)
	_update_remote_players(sim.get_remote_players())



func _close_net_menu() -> void:
	menu_open = false
	if net_menu != null:
		net_menu.queue_free()
		net_menu = null
	if not game_menu_open:
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

# One tracer per shot: a thin box stretched between muzzle and impact, faded out
# over TRACER_SECONDS. Presentation only; the sim never reads it back.
func _spawn_tracer(from: Vector3, to: Vector3) -> void:
	var length := from.distance_to(to)
	if length < 0.05:
		return
	var node := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(0.02, 0.02, 1.0)
	node.mesh = mesh
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(1.0, 0.85, 0.35, 0.85)
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	node.material_override = material
	add_child(node)
	node.scale = Vector3(1.0, 1.0, length)
	node.position = from.lerp(to, 0.5)
	# A box points down -Z after look_at, which is the axis its length runs along.
	node.look_at(to, Vector3.UP)
	tracers.append({"node": node, "life": TRACER_SECONDS, "material": material})

func _age_tracers(delta: float) -> void:
	for i in range(tracers.size() - 1, -1, -1):
		var entry: Dictionary = tracers[i]
		entry["life"] = float(entry["life"]) - delta
		var life: float = float(entry["life"])
		if life <= 0.0:
			var dead: MeshInstance3D = entry["node"]
			dead.queue_free()
			tracers.remove_at(i)
		else:
			var material: StandardMaterial3D = entry["material"]
			material.albedo_color.a = 0.85 * (life / TRACER_SECONDS)

func _update_remote_players(players: Array) -> void:
	var alive := {}
	for entry in players:
		var slot := int(entry["slot"])
		alive[slot] = true
		var node: MeshInstance3D = remote_nodes.get(slot)
		if node == null:
			node = MeshInstance3D.new()
			var capsule := CapsuleMesh.new()
			# AUD-08: hull dimensions come from movement.toml via the bridge.
			capsule.radius = sim.get_player_radius()
			capsule.height = sim.get_stand_height()
			node.mesh = capsule
			var material := StandardMaterial3D.new()
			material.albedo_color = Color(0.95, 0.35, 0.25)
			material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			node.material_override = material
			# Enemy outline: a slightly larger hull drawn with front faces culled,
			# so only its far side shows and the silhouette gets a red rim.
			# Pre-team M6: every remote player is "the enemy"; M8 teams decide.
			var outline := MeshInstance3D.new()
			var shell := CapsuleMesh.new()
			shell.radius = capsule.radius + 0.025
			shell.height = capsule.height + 0.05
			outline.mesh = shell
			var outline_material := StandardMaterial3D.new()
			outline_material.albedo_color = Color(1.0, 0.12, 0.08)
			outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			outline_material.cull_mode = BaseMaterial3D.CULL_FRONT
			outline.material_override = outline_material
			node.add_child(outline)
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
		var stand_h: float = sim.get_stand_height()
		var height: float = sim.get_crouch_height() if crouched else stand_h
		node.scale = Vector3(1.0, height / stand_h, 1.0)
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

func _place_camera(alpha: float) -> void:
	# One tick behind the simulation, blended: the newest tick is what the client
	# already has, the previous one is what it is moving away from. Angle is
	# per frame (mouse), position is per tick - without this the view glides
	# while the body jumps a grid.
	var blend: float = clampf(alpha, 0.0, 1.0)
	var position: Vector3 = sim.get_prev_position().lerp(sim.get_position(), blend)
	var eye: float = lerpf(sim.get_prev_eye_height(), sim.get_eye_height(), blend)
	camera.position = position + Vector3(0.0, eye, 0.0)
	# sim yaw 0 looks down +X and maps to Godot (x, z, -y)
	camera.rotation = Vector3(pitch, atan2(-cos(yaw), sin(yaw)), 0.0)

func _process(delta: float) -> void:
	if sim == null or camera == null:
		return
	if reconnect_check and not reconnect_done and sim.is_networked() and sim.get_joined():
		if int(sim.get_tick()) > 64:
			reconnect_done = true
			_do_reconnect()
			# AUD-17 gate: the second call must be refused, or a double click on
			# Reconnect would stack a second join on top of the first one.
			print("breakpoint: reconnect double=%d" % (1 if _do_reconnect() else 0))
	if menu_open or game_menu_open:
		# AUD-02: a menu must not stop the network pump. The server drops a link
		# after 320 ticks (5 s) without a packet, and a paused host would stop
		# stepping its own server, so keep pumping at 64 Hz with a zero MoveInput
		# (the pose is frozen because the camera update below still does not run).
		accumulator = 0.0
		if sim.is_networked():
			sim.set_input(0.0, 0.0, yaw, pitch, 0)
			menu_accumulator += delta
			var pumps := 0
			while menu_accumulator >= TICK and pumps < 5:
				sim.step_network()
				menu_accumulator -= TICK
				pumps += 1
			if pumps == 5:
				menu_accumulator = 0.0
			_update_net_state()
			_update_hud()
		# The world keeps stepping while the menu is open (the link must not time
		# out), so the camera has to follow it: otherwise resuming snapped it back.
		_place_camera(0.0)
		# Drain while paused too: otherwise a one-second pause fills the ring with
		# stale jump/land events that all fire on resume.
		sim.drain_events()
		return

	var forward := Input.get_action_strength("bp_MoveForward") - Input.get_action_strength("bp_MoveBack")
	var right := Input.get_action_strength("bp_MoveRight") - Input.get_action_strength("bp_MoveLeft")

	# --autoplay: hold forward for two seconds. Used by the smoke check to prove
	# that input -> simulation -> camera actually works without a human.
	# Drive autoplay from simulation ticks, not frames: the frame rate must not
	# change what the demo does (and headless runs at a different rate).
	var tick := int(sim.get_tick())
	_update_hud()
	var buttons := _held_buttons()
	if fire_check and int(sim.get_tick()) < 80:
		buttons |= int(button_bits.get("Fire", 0))
	if autoplay:
		yaw = 0.9  # make remote orientation observable in the headless check
	if autoplay and tick < 128:
		# Stand still until grounded, jump once, then move. This makes the
		# event-channel check deterministic instead of depending on the spawn.
		# An edge, not an equality: a frame can cross two ticks (19 -> 21).
		if tick >= 20 and not autoplay_jumped:
			autoplay_jumped = true
			buttons |= int(button_bits.get("Jump", 0))
		if tick >= 40:
			if tick < 90:
				forward = 1.0
			else:
				right = 1.0

	sim.set_input(forward, right, yaw, pitch, buttons)

	accumulator += delta
	var ticks := 0
	while accumulator >= TICK and ticks < 5:
		if sim.is_networked():
			sim.step_network()
		else:
			sim.step()
		accumulator -= TICK
		ticks += 1
	if ticks >= 5 and accumulator >= TICK:
		# AUD-20: a frame longer than five ticks. Zeroing the accumulator shifted
		# the tick phase by a random fraction and threw the lost ticks away silently,
		# so the command tick stayed behind the server for the rest of the match.
		# Keep the sub-tick remainder and count what was skipped; the network client
		# re-anchors its own axis from the next snapshot.
		dropped_ticks += int(floor(accumulator / TICK))
		accumulator = fmod(accumulator, TICK)
		if not dropped_reported:
			dropped_reported = true
			print("breakpoint: frame drop, %d ticks skipped" % dropped_ticks)

	_update_net_state()

	if interp_check and not interp_done and int(sim.get_tick()) > 40:
		# Gate: the bridge must expose a previous tick, and while the player is
		# moving it must be a different position (equal values = no history).
		interp_done = true
		var cur: Vector3 = sim.get_position()
		var prev: Vector3 = sim.get_prev_position()
		print("interp prev=%.4f cur=%.4f gap=%.4f eye_prev=%.4f eye=%.4f blend=%.4f" % [
			prev.x, cur.x, absf(cur.x - prev.x), sim.get_prev_eye_height(),
			sim.get_eye_height(), prev.lerp(cur, 0.5).x
		])
		get_tree().quit()

	var events: Array = sim.drain_events()
	for event in events:
		last_event = "%s @%d" % [
			EVENT_NAMES.get(int(event["type"]), "?"), int(event["tick"])
		]
		if autoplay:
			print("event: ", last_event)
		if int(event["type"]) == EVENT_SHOT:
			shot_events += 1
			_spawn_tracer(event["a"], event["b"])

	if fire_check and not fire_reported and int(sim.get_tick()) > 90:
		fire_reported = true
		print("tracer events=%d" % shot_events)
		get_tree().quit()

	_age_tracers(delta)

	if damage_check and not damage_done and sim.is_networked() and sim.get_peer_count() >= 2:
		damage_done = true
		sim.apply_test_damage(1, 30)
		print("breakpoint: damage slot 1 hp=%d" % sim.get_player_hp(1))

	if hud_damage_check and int(sim.get_tick()) % 32 == 0:
		print("hud hp=%d armor=%d" % [sim.get_hp(), sim.get_armor()])

	# 64 Hz simulation, arbitrary display rate: blend the two newest ticks
	# instead of snapping to the newest one and holding it two or three frames.
	_place_camera(accumulator / TICK)
	var position: Vector3 = sim.get_position()

	var velocity: Vector3 = sim.get_velocity()
	var speed := Vector2(velocity.x, velocity.z).length()
	var states := ["ground", "air", "slide", "wallrun", "wallclimb", "vault"]
	var net_line := ""
	if sim.is_networked():
		net_line = "\nnet %s slot %d peers %d%s" % [
			sim.get_role(), sim.get_player_slot(), sim.get_peer_count(),
			"" if sim.get_joined() else " (connecting)"
		]
	var used := int(sim.get_impulses())
	var cap := int(sim.get_max_impulses())
	var pips := ""
	for i in cap:
		pips += "J" if i < used else "."
	hud.text = "HP %d   ARMOR %d   IMP %s %d/%d   speed %5.2f m/s   %s\npos %.1f %.1f %.1f\nlast event %s%s" % [
		sim.get_hp(), sim.get_armor(), pips, used, cap,
		speed, states[sim.get_state()],
		position.x, position.y, position.z, last_event, net_line
	]
