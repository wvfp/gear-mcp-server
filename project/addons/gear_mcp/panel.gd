@tool
extends Control

# Gear MCP — bottom-panel UI for the in-editor GDExtension server.
# Communicates with the C++ side via `Engine.get_singleton("GearMCP")`,
# which is registered at MODULE_INITIALIZATION_LEVEL_EDITOR in
# src/register_types.cpp.

const _GREEN  := Color(0.20, 0.78, 0.35)
const _RED    := Color(0.90, 0.30, 0.30)
const _GREY   := Color(0.55, 0.58, 0.62)
const _ORANGE := Color(0.95, 0.62, 0.10)
const _DIM    := Color(0.45, 0.48, 0.52)

var _api: Object = null
var _poll_timer: Timer = null
var _last_log_count: int = 0
var _filter: String = ""

# Cached UI references
var _status_dot: ColorRect
var _status_label: Label
var _tools_tree: Tree
var _log_text: RichTextLabel
var _log_count_label: Label
var _autoscroll: CheckBox
var _domain_nodes: Dictionary = {} # domain(String) -> TreeItem


func _ready() -> void:
	custom_minimum_size = Vector2(0, 320)
	_build_ui()
	_api = Engine.get_singleton("GearMCPAPI")
	if _api == null:
		# Singleton not registered yet (e.g. panel opened before GDExtension
		# finished its SCENE init). Poll until it appears.
		_poll_timer = Timer.new()
		_poll_timer.wait_time = 0.5
		_poll_timer.timeout.connect(_try_attach_api)
		add_child(_poll_timer)
		_poll_timer.start()
	else:
		_attach_api()

	# Status / log polling — also drives the connected-client counter.
	_poll_timer = Timer.new()
	_poll_timer.wait_time = 1.0
	_poll_timer.timeout.connect(_refresh_status)
	add_child(_poll_timer)
	_poll_timer.start()


func _try_attach_api() -> void:
	_api = Engine.get_singleton("GearMCPAPI")
	if _api:
		_attach_api()


func _attach_api() -> void:
	if _api == null:
		return
	# Live log streaming
	if not _api.is_connected("log_appended", _on_log_appended):
		_api.connect("log_appended", _on_log_appended)
	# Initial tool list
	_rebuild_tools_tree()
	# Initial log dump
	_refresh_log()


# ---------------------------------------------------------------------------
# UI construction
# ---------------------------------------------------------------------------

func _build_ui() -> void:
	var root := VBoxContainer.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(root)

	# ---- Status bar -------------------------------------------------------
	var status_bar := HBoxContainer.new()
	status_bar.add_theme_constant_override("separation", 12)
	root.add_child(status_bar)

	_status_dot = ColorRect.new()
	_status_dot.custom_minimum_size = Vector2(12, 12)
	_status_dot.color = _GREY
	status_bar.add_child(_status_dot)

	_status_label = Label.new()
	_status_label.text = "Gear MCP: initializing…"
	_status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_bar.add_child(_status_label)

	var refresh_btn := Button.new()
	refresh_btn.text = "↻ Refresh"
	refresh_btn.tooltip_text = "Re-read status, tools, and log from the GDExtension"
	refresh_btn.pressed.connect(_refresh_all)
	status_bar.add_child(refresh_btn)

	# ---- Split: tools | log -----------------------------------------------
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.split_offset = 360
	root.add_child(split)

	# Tools panel
	var tools_panel := VBoxContainer.new()
	tools_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(tools_panel)

	var tools_header := HBoxContainer.new()
	tools_panel.add_child(tools_header)

	var tools_title := Label.new()
	tools_title.text = "Tools"
	tools_title.add_theme_font_size_override("font_size", 13)
	tools_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tools_header.add_child(tools_title)

	# Single button with a popup menu (Enable all / Disable all / Invert)
	# instead of two side-by-side buttons — keeps the header compact.
	var bulk_menu := MenuButton.new()
	bulk_menu.text = "▾ Bulk"
	bulk_menu.tooltip_text = "Enable all / Disable all / Invert selection"
	var popup := bulk_menu.get_popup()
	popup.add_item("☑ Enable all", 1)
	popup.add_item("☐ Disable all", 2)
	popup.add_separator()
	popup.add_item("⇄ Invert selection", 3)
	popup.id_pressed.connect(_on_bulk_menu_pressed)
	tools_header.add_child(bulk_menu)

	var filter_edit := LineEdit.new()
	filter_edit.placeholder_text = "Filter (e.g. scene, file/read)"
	filter_edit.text_changed.connect(_on_filter_changed)
	tools_panel.add_child(filter_edit)

	_tools_tree = Tree.new()
	_tools_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tools_tree.hide_root = true
	_tools_tree.columns = 2
	_tools_tree.column_titles_visible = true
	_tools_tree.set_column_title(0, "Tool")
	_tools_tree.set_column_expand(0, true)
	_tools_tree.set_column_title(1, "Calls")
	_tools_tree.set_column_custom_minimum_width(1, 50)
	_tools_tree.set_column_expand(1, false)
	_tools_tree.set_column_titles_visible(false)
	# Intercept mouse clicks via _gui_input — the item_activated signal
	# is unreliable for CELL_MODE_CHECK (Tree's auto-toggle + my manual
	# toggle were double-flipping in the previous version).
	_tools_tree.gui_input.connect(_on_tree_gui_input)
	tools_panel.add_child(_tools_tree)

	# Log panel
	var log_panel := VBoxContainer.new()
	log_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(log_panel)

	var log_header := HBoxContainer.new()
	log_panel.add_child(log_header)

	var log_title := Label.new()
	log_title.text = "Log"
	log_title.add_theme_font_size_override("font_size", 13)
	log_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	log_header.add_child(log_title)

	_log_count_label = Label.new()
	_log_count_label.text = "0"
	_log_count_label.modulate = _DIM
	log_header.add_child(_log_count_label)

	_autoscroll = CheckBox.new()
	_autoscroll.text = "Auto-scroll"
	_autoscroll.button_pressed = true
	log_header.add_child(_autoscroll)

	var clear_btn := Button.new()
	clear_btn.text = "Clear"
	clear_btn.pressed.connect(_on_clear_log)
	log_header.add_child(clear_btn)

	_log_text = RichTextLabel.new()
	_log_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_log_text.bbcode_enabled = true
	_log_text.scroll_following = true
	_log_text.selection_enabled = true
	_log_text.fit_content = false
	log_panel.add_child(_log_text)


# ---------------------------------------------------------------------------
# Status
# ---------------------------------------------------------------------------

func _refresh_status() -> void:
	if _api == null:
		_status_dot.color = _GREY
		_status_label.text = "Gear MCP: GDExtension not loaded"
		return
	var running: bool = _api.is_running()
	_status_dot.color = _GREEN if running else _RED
	var endpoint: String = _api.get_endpoint() if running else "(not listening)"
	var clients: int = int(_api.get_connected_clients()) if running else 0
	var total: int = int(_api.get_total_tools())
	var enabled: int = int(_api.get_enabled_tools())
	var calls: int = int(_api.get_total_call_count())
	_status_label.text = "● %s · %d client%s · %d/%d tools · %d call%s" % [
		endpoint,
		clients, "s" if clients != 1 else "",
		enabled, total,
		calls, "s" if calls != 1 else "",
	]


func _refresh_all() -> void:
	if _api == null:
		_try_attach_api()
		return
	_rebuild_tools_tree()
	_refresh_log()
	_refresh_status()


# ---------------------------------------------------------------------------
# Tools tree
# ---------------------------------------------------------------------------

func _rebuild_tools_tree() -> void:
	_tools_tree.clear()
	_domain_nodes.clear()
	if _api == null:
		return

	var tools: Array = _api.get_tools()
	# Group by domain
	var by_domain: Dictionary = {}
	for t in tools:
		var d: String = t.get("domain", "other")
		if not by_domain.has(d):
			by_domain[d] = []
		by_domain[d].append(t)

	# Tree.create_item() with no parent can only be called ONCE per tree
	# (it creates the hidden root). Every other item must be parented
	# explicitly. Get-or-create the root here, then add all domain groups
	# and leaves under it.
	var root := _tools_tree.get_root()
	if root == null:
		root = _tools_tree.create_item()
	_tools_tree.hide_root = true

	# Sort domains alphabetically for stable display
	var domains: Array = by_domain.keys()
	domains.sort()
	for domain in domains:
		var items: Array = by_domain[domain]
		var domain_node := _tools_tree.create_item(root)
		var total_d := items.size()
		var enabled_d := 0
		for it in items:
			if it.get("enabled", true):
				enabled_d += 1
		domain_node.set_text(0, "%s  (%d/%d)" % [domain, enabled_d, total_d])
		domain_node.set_metadata(0, {"is_domain": true, "domain": domain})
		domain_node.set_selectable(0, false)
		domain_node.set_custom_color(0, Color(0.55, 0.58, 0.62))
		_domain_nodes[domain] = domain_node

		for t in items:
			var tool_name: String = t.get("name", "")
			if _filter != "" and not tool_name.contains(_filter):
				continue
			var leaf := _tools_tree.create_item(domain_node)
			# cell_mode must be set BEFORE set_checked or set_text
			leaf.set_cell_mode(0, 1) # Tree.CELL_MODE_CHECK
			leaf.set_checked(0, bool(t.get("enabled", true)))
			leaf.set_text(0, tool_name)
			leaf.set_text(1, str(t.get("call_count", 0)))
			leaf.set_metadata(0, {
				"is_domain": false,
				"name": tool_name,
				"enabled": t.get("enabled", true),
			})
			leaf.set_metadata(1, {"name": tool_name})


func _on_filter_changed(new_text: String) -> void:
	_filter = new_text.strip_edges()
	_rebuild_tools_tree()


func _on_bulk_menu_pressed(id: int) -> void:
	if _api == null:
		return
	match id:
		1: # Enable all
			_api.enable_all_tools()
			_rebuild_tools_tree()
		2: # Disable all
			_api.disable_all_tools()
			_rebuild_tools_tree()
		3: # Invert
			_invert_selection()


func _invert_selection() -> void:
	# Walk the tree and flip every checked leaf, propagating each change
	# to C++. This is O(n) in the number of tools; fine for 109.
	if _api == null:
		return
	var root := _tools_tree.get_root()
	if root == null:
		return
	for d in range(root.get_child_count()):
		var domain := root.get_child(d)
		for c in range(domain.get_child_count()):
			var leaf := domain.get_child(c)
			var new_state := not leaf.is_checked(0)
			leaf.set_checked(0, new_state)
			var m = leaf.get_metadata(0)
			if m != null:
				m["enabled"] = new_state
				leaf.set_metadata(0, m)
			var name: String = m.get("name", "") if m != null else ""
			if name != "":
				_api.set_tool_enabled(name, new_state)
		_update_domain_label(domain)


# ---------------------------------------------------------------------------
# Mouse handling — intercept clicks on the checkbox column so toggling is
# reliable. Tree's internal CELL_MODE_CHECK auto-toggle would otherwise
# fight with our manual toggle and the row would never change.
# ---------------------------------------------------------------------------

func _on_tree_gui_input(event: InputEvent) -> void:
	if not (event is InputEventMouseButton):
		return
	var mb := event as InputEventMouseButton
	if not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT:
		return
	var item := _tools_tree.get_item_at_position(mb.position)
	if item == null:
		return
	# Only react when the click lands on the checkbox column of a leaf.
	# Godot 4.6 removed TreeItem.is_checkable(); the canonical test is
	# the cell mode. Column 0 is the only one with a checkbox.
	if item.get_cell_mode(0) != 1: # Tree.CELL_MODE_CHECK
		return
	var meta = item.get_metadata(0)
	if meta == null or meta.get("is_domain", false):
		return
	# Toggle manually (Tree's auto-toggle was the source of the previous
	# double-flip bug — by calling accept_event() we let the visual state
	# come from our own set_checked below, no race).
	item.set_checked(0, not item.is_checked(0))
	_apply_toggle(item)
	get_viewport().set_input_as_handled()


func _apply_toggle(item: TreeItem) -> void:
	var meta = item.get_metadata(0)
	if meta == null or meta.get("is_domain", false):
		return
	var name: String = meta.get("name", "")
	var enabled: bool = item.is_checked(0)
	if _api.set_tool_enabled(name, enabled):
		meta["enabled"] = enabled
		item.set_metadata(0, meta)
		# Update the "(enabled/total)" label in the parent domain row
		var parent := item.get_parent()
		if parent:
			_update_domain_label(parent)


func _update_domain_label(domain_node: TreeItem) -> void:
	var meta = domain_node.get_metadata(0)
	if meta == null:
		return
	var domain: String = meta.get("domain", "")
	var total := domain_node.get_child_count()
	var enabled := 0
	for i in range(total):
		var child := domain_node.get_child(i)
		if child.is_checked(0):
			enabled += 1
	domain_node.set_text(0, "%s  (%d/%d)" % [domain, enabled, total])


# ---------------------------------------------------------------------------
# Log
# ---------------------------------------------------------------------------

func _refresh_log() -> void:
	if _api == null:
		return
	var entries: Array = _api.get_log_entries(200)
	_log_text.clear()
	for e in entries:
		_append_entry(e, false)
	_last_log_count = _api.get_log_count()
	_log_count_label.text = str(_last_log_count)


func _on_clear_log() -> void:
	if _api == null:
		return
	_api.clear_logs()
	_log_text.clear()
	_last_log_count = 0
	_log_count_label.text = "0"


func _on_log_appended(timestamp_ms: int, level: String, method: String,
		tool: String, status: String, duration_ms: int, message: String) -> void:
	var entry := {
		"timestamp_ms": timestamp_ms,
		"level": level,
		"method": method,
		"tool": tool,
		"status": status,
		"duration_ms": duration_ms,
		"message": message,
	}
	_append_entry(entry, true)
	_last_log_count += 1
	_log_count_label.text = str(_last_log_count)
	# Bump the call counter in the tree as well
	if method == "tools/call" and tool != "":
		_bump_tool_count(tool)


func _append_entry(e: Dictionary, append_mode: bool) -> void:
	if not append_mode:
		_log_text.clear()
	var ts: int = int(e.get("timestamp_ms", 0))
	var level: String = e.get("level", "info")
	var method: String = e.get("method", "")
	var tool: String = e.get("tool", "")
	var status: String = e.get("status", "")
	var dur: int = int(e.get("duration_ms", 0))
	var msg: String = e.get("message", "")

	var time_str := _format_time(ts)
	var level_color := _level_color(level)
	var status_color := _status_color(status)

	var line := "[color=#888888]%s[/color] " % time_str
	line += "[color=#%s][b]%s[/b][/color] " % [level_color, level.to_upper()]
	if method == "tools/call" and tool != "":
		line += "[color=#3D85C6]%s[/color] " % method
		line += "[color=#1F2D3D][b]%s[/b][/color] " % tool
	else:
		line += "[color=#3D85C6]%s[/color] " % method
	line += "[color=#%s]%s[/color] " % [status_color, status]
	line += "([color=#888888]%dms[/color])" % dur
	if msg != "":
		line += "  [color=#666666]%s[/color]" % _escape_bbcode(msg)

	if append_mode:
		_log_text.append_text(line + "\n")
	else:
		_log_text.append_text(line + "\n")


func _bump_tool_count(tool_name: String) -> void:
	# Walk the tree to find the leaf with matching name
	var root := _tools_tree.get_root()
	if root == null:
		return
	for d in range(root.get_child_count()):
		var domain := root.get_child(d)
		for c in range(domain.get_child_count()):
			var leaf := domain.get_child(c)
			var m = leaf.get_metadata(0)
			if m != null and m.get("name", "") == tool_name:
				var n := int(leaf.get_text(1)) + 1
				leaf.set_text(1, str(n))
				return


func _format_time(ms: int) -> String:
	var t := Time.get_datetime_dict_from_system()
	# Use a relative or just local time. ms is unix epoch ms; convert.
	var secs := ms / 1000
	var dt := Time.get_datetime_dict_from_unix_time(secs)
	return "%02d:%02d:%02d.%03d" % [dt.hour, dt.minute, dt.second, ms % 1000]


func _level_color(level: String) -> String:
	match level:
		"error": return "CC4C4C"
		"warn":  return "E69138"
		_:       return "6FA8DC"


func _status_color(status: String) -> String:
	match status:
		"ok":       return "6AA84F"
		"error":    return "CC4C4C"
		"skipped":  return "B45F06"
		_:          return "888888"


func _escape_bbcode(s: String) -> String:
	return s.replace("[", "[lb]") \
			.replace("]", "[rb]") \
			.replace("\n", " ") \
			.replace("\r", "")
