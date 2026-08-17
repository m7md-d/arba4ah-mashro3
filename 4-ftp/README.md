# 4-ftp

Upload and download files to/from a server over a single TCP connection, driven by a small keyboard UI.

## How it works

- On connect, the server sends a listing of `files/`; arrow keys move the selection.
- `u` uploads: the client prompts for a local file path and streams it to the server, which saves it under `files/`.
- `d` downloads the selected file; the client saves it under `downloads/`.
- `q` disconnects.
- Only one client is served at a time.

File data is framed with a small header (`name size\n`) so it doesn't get confused with the menu text on the same connection — a plain `nc`/`telnet` can browse the menu but can't upload/download, use the provided client for that.

## Run

```
gcc server.c -o server
./server
```

## Connect

```
gcc client.c -o client
./client               # connects to 127.0.0.1:8080
./client <ip> <port>   # or a specific server/port
```
