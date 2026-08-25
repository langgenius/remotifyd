# Remotify daemon

`remotifyd` is a small native device daemon built from libuv, libcurl, iconv,
and the operating-system process APIs exposed by libuv. The implementation is
asynchronous, but deliberately keeps ownership centralized: each process has
one libuv loop, and every object that survives a callback has one explicit
owner and one explicit release boundary.

This document describes the code that exists in `daemon/src`. It is primarily
a lifecycle guide. File names are useful only after it is clear which object
owns a resource and how long that resource must remain valid.

## Runtime model

One release artifact runs in two roles:

```text
remotifyd
  parent process
    EventLoop
    authenticated WebSocket
    Executor child process handle
    parent -> Executor anonymous pipe
    Executor -> parent anonymous pipe

remotifyd executor
  child process
    ExecutorWorker
    independent libuv loop
    command process handles and their stdio pipes
```

The parent and Executor never share C objects. They exchange fixed IPC frames
through the two anonymous pipes created while the Executor is launched. The
Executor may launch multiple command processes, but all callbacks in one
process are serialized by that process's libuv loop.

Operational logs use inherited stderr. IPC uses only the Executor's stdin and
stdout, so log bytes can never corrupt an IPC frame.

## Ownership map

| Resource | Owner | Lifetime ends when |
| --- | --- | --- |
| `EventLoop` | `process_main` stack | `uv_run` returns |
| `ExecutorWorker` | `executor_worker_run` stack | the child loop returns |
| `CURLM` | parent `EventLoop` | the parent process shuts down |
| `CURL` WebSocket easy handle | one connection attempt | connection cleanup removes and destroys it |
| per-socket `CurlSocketWatch` | one curl-published socket | its `uv_close` callback runs |
| queued `WebSocketSend` | WebSocket send queue | sent successfully or discarded with the failed connection |
| incomplete WebSocket message | parent `EventLoop` | one WebSocket message boundary is reached |
| incomplete IPC body | the receiving loop's `IpcReadBuffer` | one complete IPC frame is handled |
| `ExecutorCommand` | child command lifecycle | every process and stdio handle has completed `uv_close` |
| stdout/stderr `iconv_t` | one `ExecutorCommand` output pipe | the last command handle closes |

The important libuv rule is that calling `uv_close` does not release the
handle immediately. The handle and every object containing it must stay alive
until the close callback runs. The code therefore treats close callbacks as
the actual ownership boundary.

## Process lifetime

`main` initializes process-wide libcurl state before selecting a role. It then
enters one of three independent paths:

```text
enroll command   -> synchronous enrollment request -> exit
executor role    -> ExecutorWorker loop             -> exit
parent role      -> EventLoop                        -> normally long-lived
```

The parent performs startup in this order:

```text
load daemon State
    -> initialize uv_loop_t
    -> initialize the control timer
    -> initialize the WebSocket send timer
    -> initialize the long-lived CURLM
    -> launch the Executor and start its output reader
    -> schedule the first WebSocket connection at delay 0
    -> uv_run
```

`EventLoop` is allocated on `process_main`'s stack and outlives `uv_run`.
Process-wide handles are embedded in it. Curl socket watchers are allocated
per socket published by libcurl; each watcher points back to the same
`EventLoop`.

The Executor has a different stack object and a different loop. It adopts its
inherited stdin and stdout as libuv pipes, starts the IPC input reader, and
never accesses the parent's `EventLoop` memory.

## WebSocket connection lifetime

There are two curl lifetimes:

- `CURLM` is initialized once and reused across reconnects.
- `CURL` and its authorization header list belong to one connection attempt.

The multi handle integrates curl's internal TCP, TLS, and WebSocket Upgrade
state machine with libuv:

```text
connect_remotify
    -> initialize CURL easy handle
    -> add easy handle to CURLM
    -> curl_multi_socket_action(CURL_SOCKET_TIMEOUT)
    -> curl asks which socket readiness each published socket needs
    -> one CurlSocketWatch is created per published socket
    -> libuv observes readable/writable on that watcher
    -> curl_socket_ready calls curl_multi_socket_action
    -> curl advances its private state machine
    -> CURLMSG_DONE confirms Upgrade completion
    -> CURLINFO_ACTIVESOCKET identifies the winning watcher
    -> non-active watchers close; the winner switches to websocket_socket_event
```

libuv does not perform TLS or WebSocket Upgrade. It only reports socket
readiness. `curl_multi_socket_action` is the trigger that lets libcurl advance
the protocol state it already owns.

The parent has one control timer. During connection establishment, libcurl's
timer callback uses it for curl deadlines. The same handle is later reused to
defer cleanup by one event-loop turn and to schedule the fixed reconnect delay.
These phases are serialized because one `EventLoop` owns only one WebSocket.

After Upgrade, curl multi no longer drives payload I/O. `curl_ws_recv` and
`curl_ws_send` operate directly on the connected easy handle while libuv keeps
watching the underlying socket.

### Incoming message lifetime

Each `websocket_read` invocation uses a 128 KiB stack read buffer. A WebSocket
message may arrive across multiple `curl_ws_recv` calls or WebSocket frames, so
the parent keeps the assembled message in the fixed 128 KiB buffer embedded in
`EventLoop`.

```text
uv says readable
    -> repeatedly call curl_ws_recv
    -> append payload while the WebSocket message is incomplete
    -> stop only on CURLE_AGAIN
    -> decode once bytesleft == 0 and CURLWS_CONT is absent
    -> reset websocket_message_length
```

If a message exceeds the protocol limit, the remaining bytes are still read
until the normal WebSocket boundary, but they are not copied. This keeps the
next message aligned without reallocating an unbounded buffer.

Generated decoder string and byte fields borrow pointers into the assembled
message. They are valid only during that decode call. Before the EventLoop
buffer can be reused, `websocket_decode` copies the selected fields into an IPC
frame, and `dispatch_request_to_executor` makes the private copy required by
the asynchronous pipe write.

### Outgoing message lifetime

WebSocket sending is a FIFO asynchronous pipeline:

```text
producer encodes one protocol message
    -> allocates WebSocketSend and its byte buffer
    -> puts the pointer into CircularQueue
    -> the first queued item schedules websocket_send_timer at delay 0
    -> websocket_write sends from the queue head
```

`CircularQueue` stores opaque pointers only. It does not know the object type
or free queued values. Once insertion succeeds, the WebSocket send pipeline
owns `WebSocketSend`:

- `bytes` remains valid until `consumed_callback` runs.
- `sent` records how much of the current message curl accepted.
- The queue head is not removed while a partial write is pending.
- `websocket_write` frees the small `WebSocketSend` object after invoking its
  callback; the callback owns the payload-specific buffer cleanup.

The writer continues synchronously while curl accepts bytes. On `CURLE_AGAIN`
it leaves the queue head untouched, enables `UV_WRITABLE`, and returns to
libuv. Writable readiness is level-triggered, so `websocket_socket_event`
removes `UV_WRITABLE` before scheduling one new send-timer turn. The writer
adds it again only if curl still cannot accept data.

This arrangement provides three properties without blocking the loop:

1. At most one queue head is being written.
2. Partial writes retain their exact byte offset.
3. Messages leave the process in FIFO order.

### Connection cleanup and reconnect

The socket descriptor is owned by libcurl. Error paths must not close it
directly. `close_websocket_socket` calls the platform's `shutdown` operation so
the poll/read side observes the connection ending; libcurl remains responsible
for the descriptor itself.

Pre-Upgrade failures schedule cleanup directly. During payload I/O, ordinary
read closure or `UV_DISCONNECT` enters the connection cleanup path. Cleanup is
deferred through the zero-delay control timer so curl and libuv resources are
not destroyed inside the callback that is currently advancing them.

The cleanup order is intentional:

```text
stop websocket_send_timer
    -> stop every curl socket watcher
    -> remove the easy handle from CURLM
    -> recover the easy handle's header-list pointer
    -> curl_easy_cleanup
    -> free the recovered header list
    -> clear connection message state
    -> uv_close every remaining socket watcher
    -> wait for the last watcher close callback
    -> discard the old connection's send queue
    -> schedule connect_remotify after 5 seconds
```

Queued frames are connection-owned and are never replayed after reconnect.
Discarding the queue invokes every `consumed_callback` with `succeed = false`,
so payload owners still receive their ordinary release notification.

A `CurlSocketWatch` is freed only from its poll close callback, after libuv has
released the embedded `uv_poll_t`. Curl remains the sole owner of the socket
descriptor itself.

## IPC lifetime

IPC is a byte stream, not a message-oriented pipe. Both receiving sides use
the same three-stage state machine:

```text
read fixed IpcHeader
    -> read exactly header.body_length bytes
    -> handle one complete frame
    -> reset IpcReadBuffer
    -> continue with any bytes remaining in the same uv read buffer
```

The libuv allocation callback owns only the temporary bytes for one read
callback. That allocation is always freed when the callback finishes.
`IpcReadBuffer` owns only a body that remains incomplete across callbacks; it
is freed immediately after the complete frame handler returns.

### Parent to Executor

`dispatch_request_to_executor` cannot write pointers from a decoded WebSocket
message directly. `uv_write` may return before the operating system consumes
the buffers, so the function allocates one `uv_write_t` followed by private
header and body bytes. The completion callback frees that allocation. If
submission fails synchronously, no completion callback will run and the caller
releases it immediately.

### Executor to parent

`executor_worker_output` applies the same rule. It copies the header and body
into memory that survives the function return, submits both byte ranges with
one `uv_write`, and releases them only from the write completion callback.

The extra copies are deliberate. They are not the most memory-efficient
possible design, but they make the asynchronous ownership rule local and
visible, reducing the risk of leaks and use-after-free errors in later changes.

`IpcProcessCallbacks` is a parent-owned correlation list for asynchronous
request completion. Its entries grow naturally, newer entries are appended at
the head, and expiration removes the oldest tail prefix. The container belongs
to `EventLoop`; individual entries live from `put` until explicit removal,
expiry, or a complete clear.

## Command process lifetime

One Exec request allocates one `ExecutorCommand`. It owns:

```text
one uv_process_t
one stdin uv_pipe_t
one stdout uv_pipe_t
one stderr uv_pipe_t
one iconv_t per output pipe, initialized on its first non-ASCII output
one small pending-byte suffix per output pipe
```

The command startup sequence is:

```text
allocate ExecutorCommand
    -> copy cmd and cwd into temporary NUL-terminated strings
    -> initialize three libuv pipes
    -> uv_spawn with those pipes as stdio
    -> free temporary cmd and cwd after uv_spawn returns
    -> start independent stdout and stderr readers
    -> write stdin once, then close the stdin pipe
```

The temporary command strings need to survive only through `uv_spawn`.
`ExecutorCommand` must survive much longer because every command handle stores
it in `handle->data`.

Two small counters express two different lifecycle facts:

- `terminal_events` begins at three. Process exit, stdout EOF, and stderr EOF
  each consume one event; whichever consumes the last event emits the one
  terminal IPC report.
- `open_handles` answers whether any process or stdio handle can still invoke a
  close callback. It is the allocation-release authority.

The process exit callback closes the `uv_process_t`; it does not end output
reading. stdout and stderr may still contain buffered bytes after process exit,
so each reader continues until its own EOF and closes its own pipe. stdin is
closed after the one-shot input write completes, or immediately when input is
empty.

Every successfully initialized handle increments `open_handles`. Every handle
uses the same close callback, which decrements it. Only the callback that makes
the count zero closes both iconv converters and frees `ExecutorCommand`.
Consequently, process exit, stdout EOF, stderr EOF, and stdin completion may
arrive in any order. Terminal readiness and allocation lifetime do not share
state or infer one another.

### Output conversion lifetime

stdout and stderr are decoded independently because they are separate byte
streams. ASCII is emitted immediately without choosing an encoding because it
has the same representation in UTF-8 and the native code page. The first
non-ASCII sequence selects UTF-8 or the native encoding; that pipe then retains
one `iconv_t` until the command object is released. If later bytes cannot be
decoded with that choice, all remaining bytes on that pipe are emitted as
ASCII hexadecimal prefixed with `[remotify stdout_hex]` or
`[remotify stderr_hex]`, so output is never silently discarded.

Each output read uses one temporary allocation laid out as:

```text
[ pending prefix ][ 128 KiB libuv read area ][ IPC report header ][ UTF-8 output ]
```

The pending prefix lets an incomplete multibyte character from the previous
read sit directly before the new bytes. `iconv` consumes the combined range.
Only an incomplete trailing suffix is copied back into the small pending array
inside `ExecutorCommand`; the large read allocation is freed before the read
callback returns.

Converted output is copied by `executor_worker_output` before that temporary
allocation is freed, so the pipe write never refers to released conversion
memory.

## End-to-end Exec flow

```text
backend binary WebSocket message
    -> parent assembles one complete WebSocket message
    -> generated protocol decoder borrows fields from that message
    -> parent constructs and asynchronously writes one IPC request
    -> Executor IPC state machine handles the complete request
    -> Executor launches the command
    -> stdout/stderr reader converts one chunk to UTF-8
    -> Executor asynchronously writes one IPC report
    -> parent IPC state machine handles the complete report
    -> generated protocol encoder creates one WebSocket message
    -> FIFO send queue writes it without blocking the EventLoop
```

There is no automatic command retry. A failed WebSocket connection discards
queued frames from that connection instead of replaying an execution or report
whose remote outcome may be uncertain.

## Current implementation boundary

The lifecycle machinery above is present, but the daemon is still being
completed incrementally. In the current source:

- Exec output and terminal reports travel from Executor to WebSocket.
- The `IPC_SINGLE` process-start report, including the local process ID and
  spawn failure result, is not connected yet.
- `IpcProcessCallbacks` is implemented but is not yet used by the Exec path.
- The long-running parent has no graceful signal-driven shutdown sequence yet;
  process-wide libcurl cleanup runs only after its event loop returns.

These are implementation gaps, not reasons to add another transport, queue,
thread, or process-lifecycle abstraction.

## Build

The daemon requires CMake, libuv, libcurl with WebSocket support, and iconv.

```sh
cmake -S daemon -B daemon/build
cmake --build daemon/build --parallel
```

Generated device protocol sources live in `daemon/src/device_protocol_gen.*`.
They are produced from the shared Go declarations under `protocol/schema`:

```sh
make -C protocol generate
```
