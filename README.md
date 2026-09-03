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
| Tool                                  | Description |
|---------------------------------------|-------------|
| `launch_qemu`                         | Spawns a QEMU instance booting an ISO, with GDB stub, dual QMP sockets, and serial logging wired up |
| `get_registers`                       | Reads the current CPU register state (`rax`–`r15`, `rip`, `eflags`, segment registers) |
| `read_memory` / `write_memory`       | Reads/writes guest memory via the GDB stub |
| `set_breakpoint` / `remove_breakpoint` | Manages software breakpoints via the GDB stub |
| `continue_execution` / `step`        | Resumes or single-steps execution; blocks until the next stop |
| `capture_screen`                      | Returns the current framebuffer as a PNG image |
| `read_serial_log`                     | Returns serial console output captured since launch (tail, default last 4000 bytes) |
| `read_event_log`                      | Returns timestamped QMP lifecycle events captured since launch |
| `ping`                                | Health check |

`launch_qemu` takes the ISO path, memory size, machine type, GDB port, and extra QEMU flags as parameters (with sane defaults), so the server isn't tied to any one project's layout.

## Architecture
```
+---------------------+
|   MCP client          |  (e.g. Claude Code)
|   (JSON-RPC / stdio)   |
+----------+-----------+
           |
+----------v-----------+
|    qemu-mcp server     |
|                         |
|  +------------------+  |      +----------------+
|  | GDB RSP client     |--+------>  QEMU gdbstub   |  (TCP :1234)
|  +------------------+  |      +----------------+
|  +------------------+  |      +----------------+
|  | QMP client (cmds)  |--+------>  QEMU QMP #1    |  (unix socket, on-demand)
|  +------------------+  |      +----------------+
|  +------------------+  |      +----------------+
|  | QMP event listener |--+------>  QEMU QMP #2    |  (unix socket, persistent,
|  | (background thread)|  |      +----------------+   background thread)
|  +------------------+  |      +----------------+
|  +------------------+  |      |  serial log file |
|  | Serial log reader  |--+------>                |
|  +------------------+  |      +----------------+
+----------+------------+
           | spawns/owns
+----------v-----------+
|   QEMU (child process)  |
+-------------------------+
```

Two separate QMP sockets are used because QEMU only accepts one active client per QMP socket at a time: one socket serves on-demand commands (capture_screen), the other is held open continuously by a background thread for event collection, so they never contend with each other.

The server owns the QEMU process it launches - it can detect if QEMU dies unexpectedly, and tears it down when the session ends.

## Building
Prerequisites:
1) `cmake`
2) C++17 compiler (`g++`)
3) `curl`
4) `qemu-system-x86_64`

```
git clone https://github.com/<your-username>/qemu-mcp.git
cd qemu-mcp
curl -sL -o third_party/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
curl -sL -o third_party/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
mkdir build && cd build
cmake .. && make
```

## Using it with Claude Code
Register it as a user-scoped MCP server so it is available in any project, not just the one in the directory:
```
claude mcp add --transport stdio --scope user qemu-mcp -- /absolute/path/to/qemu-mcp/build/qemu-mcp
```

Verify it connected:
```
claude mcp list
```

Start (or restart) a Claude Code sesion in your kernel project and the tools above will be available automatically. 

## Status
v1 complete. All 12 tools implemented and verified against a real bare-metal OS (FinchOS project); booted via ISO, register state confirmed, byte accurate at the CPU reset vector, real framebuffer PNGs captured, and `continue`/`step` proven to actually drive CPU execution. Registered as a working MCP server in Claude Code. 

## License
[PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0) — free to use, modify, and share for any noncommercial purpose, with attribution required. No commercial use.