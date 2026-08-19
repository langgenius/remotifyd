#include "event_loop.h"
#include "executor_worker.h"

#include <stdlib.h>
#include <string.h>
#include <uv.h>

int main(int argc, char **argv) {
    EventLoop event_loop = {0};

    /*
     * Main and Executor are two processes of the same release artifact. The
     * internal argument selects the child role and prevents recursive spawn.
     */
    if (argc == 2 &&
        strcmp(argv[1], "executor") == 0) {
        return executor_worker_run();
    }

    /*
     * main owns the complete EventLoop lifetime, so no asynchronous callback
     * can outlive the structure containing its handles.
     */
    if (uv_loop_init(&event_loop.loop) != 0) {
        return EXIT_FAILURE;
    }

    if (uv_timer_init(
            &event_loop.loop,
            &event_loop.timer
        ) != 0) {
        uv_loop_close(&event_loop.loop);
        return EXIT_FAILURE;
    }

    /*
     * EventLoop owns the only timer for its entire lifetime. Callbacks reuse
     * this handle instead of allocating and closing a timer for each action.
     */
    event_loop.timer.data = &event_loop;

    /*
     * main selects the process role and delegates the complete child-process
     * setup to its concrete Executor owner.
     */
    if (!launch_executor(
            &event_loop,
            argv[0]
        )) {
        uv_loop_close(&event_loop.loop);
        return EXIT_FAILURE;
    }

    return event_loop_run(&event_loop);
}
