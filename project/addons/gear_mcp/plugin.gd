@tool
extends EditorPlugin

# Gear MCP — editor panel entry point.
# Loaded automatically by Godot when the addon is enabled (plugin.cfg
# references this script as the plugin's main script).

const PanelScene = preload("res://addons/gear_mcp/panel.gd")

var _panel: Control = null


func _enter_tree() -> void:
	_panel = PanelScene.new()
	_panel.name = "GearMCPanel"
	add_control_to_bottom_panel(_panel, "Gear MCP")


func _exit_tree() -> void:
	if _panel:
		remove_control_from_bottom_panel(_panel)
		_panel.queue_free()
		_panel = null


func _get_plugin_name() -> String:
	return "Gear MCP"
