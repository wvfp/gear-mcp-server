#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// GearMainThreadNode — drains the MainThreadQueue each frame.
//
// Lives in the editor's scene tree root. Its _process() callback is invoked
// on the Godot main thread every frame, which lets the worker TCP threads
// submit work via MainThreadQueue::invoke_on_main() and have it executed
// on the main thread (the only thread where most Godot API calls are
// guaranteed to be safe).
// ---------------------------------------------------------------------------
class GearMainThreadNode : public godot::Node {
    GDCLASS(GearMainThreadNode, godot::Node)

protected:
    static void _bind_methods();

public:
    GearMainThreadNode();
    ~GearMainThreadNode();

    // Called by the engine on the main thread every frame while the node
    // is in the scene tree with set_process(true).
    void _process(double delta) override;
};

} // namespace gear_mcp
