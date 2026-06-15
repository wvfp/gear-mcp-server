extends Node2D
# Screenshot helper: takes a screenshot of the viewport and saves it to a file.
# Used by the MCP `run_scene_with_timeout` tool to capture the play window.
#
# This script lives at res://addons/gear_mcp/screenshot_helper.gd so it
# ships with the plugin and doesn't need to be copied into each project.
# Use the MCP `attach_screenshot_helper` tool to attach it to a scene
# once; the attachment is persisted in the .tscn file.
#
# Design rules (do not violate):
#   * This script NEVER calls get_tree().quit(). The MCP tool is the sole
#     owner of the play loop: it calls run_scene_with_timeout, which
#     launches, waits, captures, and stops. The helper must not race the
#     tool's stop_playing_scene call.
#   * Capture is idempotent: _captured guards re-entry so multiple frames
#     at or past wait_frames still produce a single PNG.
#   * Errors (no viewport, save_png != OK) are reported via print() and
#     exposed through the last_error export so callers can read them
#     via the editor's remote inspector if needed.

@export var screenshot_path: String = "res://screenshot.png"
@export var wait_frames: int = 60

# Sidecar filename. The MCP run_scene_with_timeout tool writes the
# caller's requested output_path into this file just before launching
# the scene, so the helper knows where to write. If the sidecar is
# missing (e.g. someone pressed Play in the editor manually), the
# helper falls back to the screenshot_path export.
const SIDECAR_PATH: String = "res://mcp_screenshot_target.txt"

var _frame_count: int = 0
var _captured: bool = false
var last_error: String = ""

func _ready() -> void:
	# Diagnostic breadcrumb: write a status file as soon as _ready runs.
	# This proves the play subprocess actually launched the scene and the
	# script is running. The file is overwritten on every play, so a
	# stale breadcrumb is overwritten by the next run.
	var diag_path: String = "res://screenshot_helper_diag.txt"
	var df := FileAccess.open(diag_path, FileAccess.WRITE)
	if df:
		df.store_string("_ready ran at frame %d, wait_frames=%d, default_screenshot_path=%s\n" % [
			Engine.get_process_frames(), wait_frames, screenshot_path
		])
		df.close()

	# If MCP left a sidecar file, read it and redirect the screenshot
	# there. This keeps the C++ tool and the helper script in agreement
	# about where the file ends up on disk.
	if FileAccess.file_exists(SIDECAR_PATH):
		var f := FileAccess.open(SIDECAR_PATH, FileAccess.READ)
		if f:
			var p := f.get_as_text().strip_edges()
			f.close()
			if p != "":
				screenshot_path = p
	print("[ScreenshotHelper] _ready, will capture in ", wait_frames, " frames to ", screenshot_path)

	# Update the diag file with the final path (after sidecar override).
	df = FileAccess.open(diag_path, FileAccess.WRITE)
	if df:
		df.store_string("_ready ran at frame %d, wait_frames=%d, final_screenshot_path=%s\n" % [
			Engine.get_process_frames(), wait_frames, screenshot_path
		])
		df.close()

func _process(_delta: float) -> void:
	if _captured:
		return
	_frame_count += 1
	if _frame_count >= wait_frames:
		_capture_and_save()

func _capture_and_save() -> void:
	_captured = true
	var vp := get_viewport()
	if vp == null:
		last_error = "viewport is null"
		print("[ScreenshotHelper] ", last_error)
		return
	var tex := vp.get_texture()
	if tex == null:
		last_error = "viewport.get_texture() is null"
		print("[ScreenshotHelper] ", last_error)
		return
	var img: Image = tex.get_image()
	if img == null:
		last_error = "get_image() returned null"
		print("[ScreenshotHelper] ", last_error)
		return
	print("[ScreenshotHelper] image size: ", img.get_width(), "x", img.get_height())
	var err: int = img.save_png(screenshot_path)
	print("[ScreenshotHelper] save_png(", screenshot_path, ") returned ", err)
	if err != OK:
		last_error = "save_png error %d" % err
