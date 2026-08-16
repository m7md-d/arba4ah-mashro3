# 1-chat

A multi-client TCP chat server. Handles every connection in a single thread with `select()`.

## How it works

- A client connects and is prompted for a username.
- Every message is broadcast to all other registered clients.
- A newly joined client receives the full chat history so far.
- Join/leave events are broadcast to the room.

Up to 30 clients at a time; history is capped at 16 KB.

## Run

```
gcc server.c -o server
./server
```

## Connect

```
nc localhost 8080
```
