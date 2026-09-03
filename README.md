# qemu-mcp
An MCP (Model Context Protocol) server for debugging bare-metal kernels running in QEMU, built in C/C++.

## Why
Most tooling for AI-assisted QEMU debugging assumes a booted guest OS you can SSH into, or a desktop environment you can drive with mouse/keyboard automation. Neither fits bare-metal kernel development, where the guest might be a kernel that triple faults before any of that exists, and the only ground truth is the serial console, the raw framebuffer, and CPU/memory state via the GDB stub. 

`qemu-mcp` gives an LLM coding agent (e.g. Claude Code) direct, structured access to a QEMU instance running a bare-metal target: it can inspect registers and memory, set breakpoints, step execution, read the full serial history, and - critically - actually see what's on screen, instead of trusting logs or assuming a fix worked.

It's built as an opt-in debugging tool, not a permanent layer over your normal QEMU workflow: you launch it only when you need it, and it owns the QEMU process it spawns for the duration of the session.

## What it does
- **CPU/memory inspection** - talks the GDB Remote Serial Protocol directly to QEMU's `-s -S` gdbstub over a raw TCP socket (hand-rolled `$...#checksum` packets, no external GDB binary required)
- **Execution control** - set/remove breakpoints, single-step, continue
- **Persistent serial capture** - QEMU's serial output is logged to disk from the moment it launches, so nothing is lost between polls
- **Screen capture** - pulls the current framebuffer via QMP's `screendump`, working with or without a guest OS, so the agent can visually verify what's actually being rendered
- **Event log** - subscribes to QMP events (`RESET`, `SHUTDOWN`, `GUEST_PANICKED`, etc.) and timestamps them as they happen, so "what happened right before it died" is answerable after the fact

## Tools
| Tool                              | Description |
|-----------------------------------|-------------|
| `launch_qemu`                     | Spawns a QEMU instance for the target kernel/image, with GDB stub, QMP, and serial logging wired up |
| `get_registers`                   | Reads the current CPU register state |
| `read_memory` / `write_memory`    | Reads/writes guest physical or virtual memory |
| `set_breakpoint` / `remove_breakpoint` | Manages breakpoints via the GDB stub |
| `continue_execution` / `step`     | Resumes or single-steps execution |
| `read_serial_log`                 | Returns serial console output captured since launch |
| `capture_screen`                  | Returns the current framebuffer as an image |
| `read_event_log`                  | Returns timestamped QMP lifecycle events (resets, panics, shutdowns) |

`launch_qemu` takes the kernel path, memory size, machine type, and extra QEMU flags as parameters (with sane defaults), so the server isn't tied to any one project's layout.

## Architecture
```
┌─────────────────────┐
│   MCP client         │  (e.g. Claude Code)
│   (JSON-RPC / stdio)  │
└──────────┬───────────┘
           │
┌──────────▼───────────┐
│    qemu-mcp server    │
│                        │
│  ┌──────────────────┐  │      ┌───────────────┐
│  │ GDB RSP client    │──┼──────▶  QEMU gdbstub  │  (TCP :1234)
│  └──────────────────┘  │      └───────────────┘
│  ┌──────────────────┐  │      ┌───────────────┐
│  │ QMP client         │──┼──────▶  QEMU QMP     │  (unix socket)
│  └──────────────────┘  │      └───────────────┘
│  ┌──────────────────┐  │      ┌───────────────┐
│  │ Serial log reader  │──┼──────▶  log file      │
│  └──────────────────┘  │      └───────────────┘
└──────────┬────────────┘
           │ spawns/owns
┌──────────▼───────────┐
│   QEMU (child process) │
└────────────────────────┘
```

The server owns the QEMU process it launches - it can detect if QEMU dies unexpectedly, and tears it down when the session ends.

