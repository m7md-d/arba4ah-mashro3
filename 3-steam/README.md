# 3-steam

Streams a webcam feed to a connected terminal, rendered live as colored ASCII art.

## How it works

- Captures frames from `/dev/video0` via V4L2 (YUYV format).
- Converts each frame into a grid of ANSI-colored characters sized to the client's terminal (queries the terminal size with `\033[18t` on connect, falls back to a default after 1s).
- Pushes a new frame to the client each time the camera has one ready.
- Client sends `q` to disconnect.

## Run (Linux only — needs a webcam and V4L2)

```
gcc server.c -o server
./server
```

## Connect

```
nc localhost 8080
```

Works best in a terminal emulator that supports ANSI 256-color; a client that doesn't answer the size query just gets the default 160x48 framing.
