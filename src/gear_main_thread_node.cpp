#include "gear_main_thread_node.h"

#include "server/main_thread_queue.h"

using namespace godot;

namespace gear_mcp {

GearMainThreadNode::GearMainThreadNode() {}

GearMainThreadNode::~GearMainThreadNode() {}

void GearMainThreadNode::_process(double delta) {
    // Called on the Godot main thread every frame. Drain the queue so
    // any tasks submitted via MainThreadQueue::invoke_on_main() from
    // TCP worker threads are executed here.
    MainThreadQueue::process_pending();
}

void GearMainThreadNode::_bind_methods() {
    // No script-facing methods needed; this node is internal.
}

} // namespace gear_mcp
