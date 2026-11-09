# LANdrop 0.1.0

**Your files. Your network. No cloud.**

LANdrop 0.1.0 is a minimal local-network file transfer prototype. It sends one regular file over a direct TCP connection to another machine on the same reachable network.

## Current scope

- One file per TCP connection.
- IPv4/IPv6 name resolution through `getaddrinfo`.
- Streaming transfer with a 64 KiB buffer; the whole file is never loaded into RAM.
- A small binary header (`LDP0`, version, flags, filename length, file size), all integers big-endian.
- Receiver accepts only conservative ASCII basenames (`A-Z`, `a-z`, digits, `.`, `_`, `-`).
- Receiver writes to a private temporary file, then publishes without overwriting an existing destination.
- Incomplete transfers remove their temporary file.
- No cloud, account, or external server.

## Important architecture note

YLang 2.3.0 documents byte arrays and synchronous file streaming (`io.stream_open_read`, `io.stream_read`, `io.stream_open_write`, `io.stream_write`, `io.stream_close`). The inspected documentation does **not** document a TCP socket API, a process-spawn API, or a C FFI for calling this helper from a `.yl` program.

For that reason, this 0.1.0 archive is honest about the current boundary:
- `src/network/network.c` is the working TCP transport executable.
- `scripts/landrop` is a small launcher for that executable.
- `src/transfer/stream_read_probe.yl` is a standalone YLang Binary I/O probe, not falsely presented as the TCP transport.
- A future YLang networking API or supported FFI/process interface can replace this boundary so the application logic can move into YLang.

The transport helper is deliberately isolated; it is not a claim that YLang currently calls C functions directly.

## Build

Requirements: a POSIX-like system, C17 compiler, and GNU Make.

```sh
make
```

On Fedora:

```sh
sudo dnf install gcc make
```

## Receive a file

On the receiving computer, choose an existing destination directory:

```sh
./scripts/landrop receive 5050 "$HOME/Downloads"
```

The receiver waits for one incoming connection, receives one file, then exits.

## Send a file

On the sending computer:

```sh
./scripts/landrop send 192.168.1.50 5050 ./photo.jpg
```

Replace `192.168.1.50` with the receiver's LAN IP address. The sender prints the transmitted filename and byte count.

## Test locally

```sh
make test
```

The smoke test transfers a generated 128 KiB random file over loopback and compares the received bytes.

## Protocol: LDP 0.1

The fixed header is 16 bytes:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII magic `LDP0` |
| 4 | 1 | Version `0x01` |
| 5 | 1 | Reserved flags; must be zero |
| 6 | 2 | Filename length, unsigned big-endian |
| 8 | 8 | File size, unsigned big-endian |

The header is followed by the filename bytes (ASCII only in 0.1), then exactly `file_size` payload bytes. TCP provides ordered byte delivery; the implementation still loops for partial reads and writes.

## Security and limitations

- **Use only on a trusted LAN.** 0.1 has no authentication, encryption, or peer identity verification.
- The receiver binds to all local interfaces and accepts one connection. Use a host firewall and only start it when you intend to receive.
- The sender's filename is reduced to a basename. The receiver rejects path separators and unsupported characters.
- Existing destination files are never overwritten.
- No resume, multi-file transfer, directory transfer, compression, checksums, discovery, GUI, or background daemon.
- The receiver does not yet impose a configurable maximum file size. Only run it on a trusted network.
- The receiver handles one peer at a time and exits after that transfer attempt.

## YLang Binary I/O probe

If `ylang` 2.3.0 is installed, build the optional streaming-read probe:

```sh
dd if=/dev/urandom of=input.bin bs=1M count=1 status=none
ylang check src/transfer/stream_read_probe.yl
ylang build src/transfer/stream_read_probe.yl -o stream-read-probe
./stream-read-probe
```

This probe is separate from the TCP helper because the currently documented YLang API does not expose TCP sockets or a supported way to call this C helper.

## Project layout

```text
landrop/
├── Makefile
├── README.md
├── LICENSE
├── scripts/
│   └── landrop
├── src/
│   ├── network/
│   │   └── network.c
│   └── transfer/
│       └── stream_read_probe.yl
└── tests/
    └── smoke.sh
```

## License

MIT. See `LICENSE`.
