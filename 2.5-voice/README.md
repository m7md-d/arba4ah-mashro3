# 2.5-voice

Send voice notes to a server over TCP and play them back from the server's terminal.

## How it works

- `voicenote.c` is a standalone recorder: captures 5 seconds of audio from the mic (ALSA) and writes it to `output.wav`.
- `server.c` accepts a connection per note, streams the incoming bytes straight into a `.wav` file under `voice_notes/`, and reads the WAV header back to get its duration.
- The server's terminal shows the list of received notes. Arrow keys move the selection, Enter plays the selected one (`afplay` on macOS, `aplay` on Linux).

## Run

```
gcc server.c -o server
./server
```

## Send a note

A sample file (`robotSound.wav`) is included for quick testing:

```
nc localhost 8080 < robotSound.wav
```

To record your own note (Linux only, needs ALSA + a mic):

```
gcc voicenote.c -o voicenote -lasound
./voicenote
nc localhost 8080 < output.wav
```

Received notes are saved under `voice_notes/` (gitignored).
