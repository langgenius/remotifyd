#include "consts.h"
#include "curl_multi.h"
#include "enroll.h"
#include "event_loop.h"
#include "executor.h"
#include "executor_worker.h"
#include "init.h"
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

    if (uv_timer_init(&event_loop.loop, &event_loop.websocket_send_timer) != 0) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    if (uv_timer_init(&event_loop.loop, &event_loop.heartbeat_timer) != 0) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    if (uv_timer_init(&event_loop.loop, &event_loop.handshake_timer) != 0) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    if (uv_timer_init(&event_loop.loop, &event_loop.liveness_timer) != 0) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    /* Every timer is process-owned and reused for one stable responsibility. */
    event_loop.timer.data = &event_loop;
    event_loop.websocket_send_timer.data = &event_loop;
    event_loop.heartbeat_timer.data = &event_loop;
    event_loop.handshake_timer.data = &event_loop;
    event_loop.liveness_timer.data = &event_loop;

    /* The WebSocket multi handle exists before any connection is scheduled. */
    if (!init_curl_multi(&event_loop)) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    /*
     * The parent delegates the complete child-process setup to its concrete
     * Executor owner.
     */
    if (!launch_executor(&event_loop, argv[0])) {
        uv_loop_close(&event_loop.loop);
        state_free(&event_loop.state);
        return EXIT_FAILURE;
    }

    /* The first connection begins in the first turn of the running uv loop. */
    result = uv_timer_start(
        &event_loop.timer,
        connect_remotify,
        0,
        0
    );

    if (result != 0) {
        logf_error(
            "failed to schedule WebSocket connection: %s",
            uv_strerror(result)
        );
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
    int result;

    /*
     * Process-wide libraries have one lifetime regardless of the selected
     * role. Role-specific state and event loops remain inside their entries.
     */
    if (!process_init()) return EXIT_FAILURE;

    if (argc == 2 && strcmp(argv[1], "executor") == 0) {
        result = process_executor(argc, argv);
    } else if (argc >= 2 && strcmp(argv[1], "enroll") == 0) {
        result = process_enroll(argc, argv);
    } else {
        result = process_main(argc, argv);
    }

    process_cleanup();
    return result;
}
