# c-socket

A collection of small C projects built around raw TCP socket programming (BSD sockets API). Each folder is a standalone project exploring a different use case for sockets: chat, a real-time game, voice notes, video streaming, and file transfer.

## Projects

- [1-chat](1-chat/) — multi-client TCP chat server
- [2-game](2-game/) — real-time multiplayer shooting game (planned)
- [2.5-voice](2.5-voice/) — record a voice note and send it to a server that lists and plays them back
- [3-steam](3-steam/) — streams a webcam feed as ANSI-colored ASCII art to a terminal
- [4-ftp](4-ftp/) — file upload/download server with a keyboard-driven client

Every server listens on port 8080 and can be reached with a plain TCP client such as `nc` or `telnet`, unless its own README says otherwise.

## Notes

- Servers build `struct sockaddr` by hand (raw byte offsets for family/port/IP) instead of `sockaddr_in`, as a way to work with the wire format directly.
- No Makefiles — compile each project directly with `gcc`.
