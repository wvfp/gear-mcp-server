#include "main_thread_queue.h"

#include <atomic>
#include <chrono>
#include <cstdio>

namespace gear_mcp {

std::mutex MainThreadQueue::s_queue_mutex;
std::queue<MainThreadQueue::Task> MainThreadQueue::s_queue;

// When false, invoke_on_main runs tasks directly on the calling thread
// (used as fallback when the process node is not available)
static std::atomic<bool> s_dispatch_available{false};

void MainThreadQueue::set_dispatch_available(bool available) {
    s_dispatch_available = available;
}

std::string MainThreadQueue::invoke_on_main(std::function<std::string()> p_task) {
    // If the main-thread dispatch node is not set up, run the task directly.
    // Many Godot API calls work from any thread (read-only queries, etc.)
    // This is a fallback that allows tools to work without the scene tree node.
    if (!s_dispatch_available) {
        return p_task();
    }

    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done_flag = false;
    std::string result;

    {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        Task task;
        task.func = std::move(p_task);
        task.result_ptr = &result;
        task.done_mutex = &done_mutex;
        task.done_cv = &done_cv;
        task.done_flag = &done_flag;
        s_queue.push(std::move(task));
    }

    // Block until the main thread processes our task, with a timeout
    std::unique_lock<std::mutex> lock(done_mutex);
    bool completed = done_cv.wait_for(lock, std::chrono::seconds(10), [&done_flag] { return done_flag; });

    if (!completed) {
        std::fprintf(stderr, "[Gear MCP] WARNING: invoke_on_main timed out after 10s\n");
        return "{\"error\": \"Main thread dispatch timed out (10s). The process node may not be receiving notifications.\"}";
    }

    return result;
}

void MainThreadQueue::process_pending() {
    // Drain all pending tasks from the queue
    while (true) {
        Task task;
        {
            std::lock_guard<std::mutex> lock(s_queue_mutex);
            if (s_queue.empty()) break;
            task = std::move(s_queue.front());
            s_queue.pop();
        }

        // Execute the task on the main thread
        std::string result;
        if (task.func) {
            result = task.func();
        }

        // Signal completion to the waiting TCP thread
        if (task.result_ptr) {
            *task.result_ptr = std::move(result);
        }
        {
            std::lock_guard<std::mutex> lock(*task.done_mutex);
            *task.done_flag = true;
        }
        task.done_cv->notify_one();
    }
}

bool MainThreadQueue::has_pending() {
    std::lock_guard<std::mutex> lock(s_queue_mutex);
    return !s_queue.empty();
}

} // namespace gear_mcp
