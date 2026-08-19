#include "consts.h"
#include "enroll.h"
#include "event_loop.h"
#include "executor_worker.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <uv.h>

static int process_main(int argc, char **argv) {
    EventLoop event_loop = {0};
    int result;

    (void)argc;

    /*
     * Only the parent owns remote credentials. Loading before uv_loop_init
     * also lets every later parent callback read one immutable State value.
     */
    if (!state_load(&event_loop.state) ||
        event_loop.state.api_base[0] == '\0' ||
        event_loop.state.token[0] == '\0') {
        logf_error(
            "please login to remotify at first, see %s/devices for help.",
            remotify_console
        );
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    /*
     * process_main owns the complete EventLoop lifetime, so no asynchronous
     * callback can outlive the structure containing its handles.
     */
    if (uv_loop_init(&event_loop.loop) != 0) {
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    if (uv_timer_init(&event_loop.loop, &event_loop.timer) != 0) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    /*
     * EventLoop owns the only timer for its entire lifetime. Callbacks reuse
     * this handle instead of allocating and closing a timer for each action.
     */
    event_loop.timer.data = &event_loop;

    /*
     * The parent delegates the complete child-process setup to its concrete
     * Executor owner.
     */
    if (!launch_executor(&event_loop, argv[0])) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    result = event_loop_run(&event_loop);
    state_free(&event_loop.state);
    return result;
}

static int process_executor(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return executor_worker_run();
}

int main(int argc, char **argv) {
    /*
     * The release artifact contains two independent process entries. Select
     * the role before either one creates any role-specific state or handles.
    */
    if (argc == 2 && strcmp(argv[1], "executor") == 0) {
        return process_executor(argc, argv);
    } else if (argc >= 2 && strcmp(argv[1], "enroll") == 0) {
        return process_enroll(argc, argv);
    } else {
        return process_main(argc, argv);
    }
}
