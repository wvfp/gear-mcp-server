#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// MainThreadQueue — dispatches tasks from TCP worker threads to the Godot
// main thread via a synchronized queue.
//
// TCP threads call invoke_on_main() which blocks until the task completes.
// The Godot main thread calls process_pending() each frame (via _process
// notification on the EditorPlugin) to drain the queue.
// ---------------------------------------------------------------------------
class MainThreadQueue {
public:
    /// Submit a task and block until it completes on the main thread.
    /// Returns the task's result string.
    static std::string invoke_on_main(std::function<std::string()> p_task);

    /// Called from the Godot main thread each frame to process queued tasks.
    static void process_pending();

    /// Set whether the main-thread dispatch node is available.
    /// When false, tasks run directly on the calling thread.
    static void set_dispatch_available(bool available);

    /// Check if there are pending tasks.
    static bool has_pending();

private:
    struct Task {
        std::function<std::string()> func;
        std::string *result_ptr;
        std::mutex *done_mutex;
        std::condition_variable *done_cv;
        bool *done_flag;
    };

    static std::mutex s_queue_mutex;
    static std::queue<Task> s_queue;
};

} // namespace gear_mcp
