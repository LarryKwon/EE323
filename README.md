# 🧪 EE323 Network Programming Assignments

This repository contains solutions to four major network programming assignments from KAIST's EE323 Computer Networks course. The assignments progressively build an understanding of socket programming, application-layer protocols, network-layer routing, and transport-layer reliability.

## 🗂 Assignments Overview

### 📡 Assignment 1: TCP Echo Server and Client

> **Goal**: Implement a simple server that accepts TCP connections and echoes client messages line by line.

- ✅ Each client runs in its own process (via `fork()`).
- ✅ Message is transmitted upon pressing `Enter`.
- ✅ Double `Enter` triggers client exit.
- ✅ Command-line interface using `getopt()` (`-p`, `-h`).
- ✅ Compatible with file redirection and piping (`< input.txt`, `> output.txt`).

**Files:**
- `server.c`
- `client.c`
- `Makefile`

**Run:**
```bash
# Server
$ ./server -p 12345

# Client
$ ./client -p 12345 -h 127.0.0.1
```

---

### 🌐 Assignment 2: HTTP Proxy Server

> **Goal**: Build a multi-client HTTP/1.0 proxy server supporting basic caching and blacklisting.

- ✅ Accepts requests, forwards them to destination servers, returns responses.
- ✅ Parses HTTP requests (method, URL, host).
- ✅ Filters blacklisted domains and redirects to `http://warning.or.kr`.
- ✅ Can be tested with browser, telnet, or Python script.
- ✅ Properly handles malformed requests with HTTP 400.

**Files:**
- `proxy.c`
- `Makefile`

**Run:**
```bash
$ ./proxy <PORT> < blacklist.txt
```

**Test (example):**
```bash
$ telnet localhost <PORT>
GET http://example.com/ HTTP/1.0
Host: example.com
```

---

### 🚦 Assignment 3: Software Router Implementation (via Mininet + POX)

> **Goal**: Simulate a software router that performs routing, ARP handling, ICMP response, and packet forwarding in a Mininet-emulated network.

- ✅ Responds to ARP requests and maintains ARP cache.
- ✅ Responds to ICMP Echo Requests and Time Exceeded (Traceroute).
- ✅ Filters packets from blacklisted subnets (10.0.2.0/24).
- ✅ Drops packets with unresolved ARP after timeout, replies with ICMP Unreachable.
- ✅ Fully tested using Mininet and POX SDN controller.

**Files:**
- `router/sr_router.c`
- `router/sr_arpcache.c`
- `router/Makefile`

**Run (inside VM):**
```bash
$ ./run_pox.sh
$ ./run_mininet.sh
$ ./sr
```

---

### 📦 Assignment 4: Reliable Transport Layer (STCP)

> **Goal**: Implement a simplified TCP-like transport protocol called STCP.

- ✅ Three-way handshake (SYN/SYN-ACK/ACK)
- ✅ Four-way connection teardown (FIN/ACK)
- ✅ In-order, reliable, full-duplex transmission
- ✅ Sliding window with slow start
- ✅ Logging transmission statistics (`client_log.txt`, `server_log.txt`)
- ✅ Compatible with `mysock` interface (provided stub)

**Files:**
- `transport.c` (ONLY file modified)
- Compiles with provided `Makefile`

**Run:**
```bash
# Server
$ ./server

# Client
$ ./client <SERVER_IP>:<PORT> -f testfile.txt
```

**Output:**
- File `rcvd` is saved at the client side.
- Logs are saved in the root directory.

---

## 🛠 Build Instructions

Each assignment contains a `Makefile`. Run:

```bash
$ make
```

To clean:

```bash
$ make clean
```

> ⚠ All code is tested and compiled on KAIST lab machines (`eelab5`, `eelab6`). Behavior may vary on other environments.

---

## 📚 References

- Beej’s Guide to Network Programming
- RFC 793 – Transmission Control Protocol
- RFC 1945 – HTTP/1.0 Specification
- Mininet & POX documentation

---

## 🚫 Academic Integrity

Do **not** reuse or redistribute this repository for academic credit. This work is intended for educational reference and self-study only.
