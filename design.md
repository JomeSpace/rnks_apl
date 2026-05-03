# Design Decisions

## Architecture and Layering

- Strict separation between application and protocol logic:
  - Application: `src/client.c` and `src/server.c` (file I/O, CLI)
  - Protocol + UDP: `src/clientSy.c` and `src/serverSy.c`
- One socket per instance, no threads

## Transport and Addressing

- UDP/IPv6 as transport layer.
- Client uses `connect()` on the UDP socket so `send()`/`recv()` can be used without specifying the destination each time.
- Server uses `recvfrom()` and remembers the client address for `sendto()`.

## Time-Synchronous Sending

- At most one packet sent (or retransmitted) per interval.
- `select()` waits for an ACK or interval end. If an ACK arrives early, the remainder of the interval is still waited out.
- Interval length: `GBN_TIMEOUT_INT_MS` (defined in `src/data.h`).

## Window Management and Ring Buffer (Client)

- GBN window tracked by `base` (smallest unacknowledged SeqNr) and `seq_num` (next SeqNr to send).
- Ring buffer `buffer[GBN_BUFFER_SIZE]` stores sent but unacknowledged packets for retransmission.
- Retransmissions always take priority over new packets.

## Timer and Retransmit

- Timeout is a multiple of intervals (`GBN_TIMEOUT_UNITS`).
- `timeout_counter` increments only when there are outstanding packets and `base` has not advanced.
- On timeout: Go-Back-N from `base`, retransmitting one packet per interval.

## ACK Handling (Client)

- Cumulative ACKs: `AnswOk.SeNo` is the next expected SeqNr.
- ACKs outside the window are discarded (`ack_no < base` or `ack_no > seq_num`).
- On progress: `base = ack_no`, `timeout_counter = 0`.

## Receiver Logic (Server)

- No receiver buffering: out-of-order packets are discarded.
- A cumulative ACK with `expected_seq` is always sent regardless.
- `expected_seq` only advances on in-order data packets.

## Connection Lifecycle

- Connection setup via `ReqHello`/`AnswHello`.
- Connection teardown via `ReqClose`/`AnswOk` after draining (all packets acknowledged).

## Loss Simulation

- Request loss (`lossReq`) applied in `processRequest()`.
- ACK loss (`lossAck`) applied in the server main loop before `sendAnswer()`.

## Diagrams

- Sequence diagrams for error-free transfer, packet loss, ACK loss, and window size comparison in `docs/sequence-diagrams/`.
- State machine diagrams for client and server in `docs/state-diagrams/`.
