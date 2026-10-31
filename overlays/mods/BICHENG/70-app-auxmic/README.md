# Auxmic

Expose a phone's network microphone as an ALSA capture device on the printer.
The existing phone page sends 48 kHz mono signed 16-bit PCM over HTTPS. Consumers
open `auxmic`; they do not need to implement the phone protocol.

```text
Phone page → HTTPS :9443 → auxmic.py → hw:Loopback,0,0
                                      snd-aloop
Recording application ← auxmic ← hw:Loopback,1,0
```

## Runtime

One Python process owns the current session, one bounded PCM buffer and the ALSA
playback handle. `auxmic_pcm.py` contains the ALSA binding and a local WAV output
for development. There is no local TCP relay or status file.

The service accepts one source at a time. A replacement clears queued samples
and invalidates the old session. The buffer holds at most two seconds; overflow drops
the oldest samples. Playback starts after one second has accumulated. This leaves
room for delayed uploads arriving in a burst. A smaller
buffer is released after a two-second priming timeout measured from the first
queued sample, so time spent granting microphone permission does not bypass
priming. A short final block
does not remain queued indefinitely. Underruns refill the buffer while writing
silence. This is a latency-bounded live input, not a lossless recorder.

With no source, the process continues writing silence to keep the device open.
Stop clears the application buffer; samples already submitted to ALSA may still
be captured briefly. A disconnected source drains its buffer and then produces
silence. Its session expires after ten seconds without a chunk. The playback
format is 48 kHz stereo S16_LE, with the mono source duplicated into both channels.

`S99auxmic` loads the module and starts the process as `lava:audio`, using the
firmware's existing `fake-service` supervisor and a 64 MiB memory limit. A fatal
output error exits the process; the supervisor retries after three seconds.
Recovery can interrupt capture, so applications may need to reopen the device.

## Requirements and operation

The firmware must contain `/lib/modules/snd-aloop.ko`, Python 3, libasound,
`fake-service`, an `audio` group with access to the loopback device nodes, and
the mod files. The kernel build step uses `CONFIG_SND_ALOOP=m` and the target
`sound/drivers/snd-aloop.ko`. The compiled module must match the running kernel.

Package these files into the firmware image. Copying them into the running root
filesystem is temporary: the normal U1 boot sequence clears its writable overlay.
Do not rely on a successful live-copy test as evidence of boot persistence.

```sh
/etc/init.d/S99auxmic start
/etc/init.d/S99auxmic status
/etc/init.d/S99auxmic restart
/etc/init.d/S99auxmic stop
```

Firmware Config shows the phone URL and pairing code. Credentials and the TLS
certificate persist in `/home/lava/printer_data/config/extended/auxmic/`. Logs go
to syslog through the supervisor. Normal chunk uploads are not logged.

The default capture alias uses the fixed loopback subdevice `hw:Loopback,1,0`.
Use one direct capture client at a time:

```sh
ffmpeg -f alsa -ar 48000 -ac 2 -i auxmic -t 10 phone.wav
curl -k https://127.0.0.1:9443/v1/auxmic/source
```

The source endpoint reports session state, device readiness, buffer occupancy,
discarded frames and output frames. `dropped_frames` includes queued samples
discarded on stop/replacement/expiry. `underrun_frames` counts padding in the
block that exhausted a primed buffer; it is not total silence duration. Counters
reset when the service restarts. An open session alone does not prove sound is
arriving; also inspect its chunk counters or record from the card.

## Local development

```sh
python3 -m unittest discover -s overlays/mods/BICHENG/70-app-auxmic/test -v
```

To run the same phone protocol without an ALSA device:

```sh
python3 overlays/mods/BICHENG/70-app-auxmic/root/usr/local/bin/auxmic.py \
  --insecure-http --config-dir /tmp/auxmic-debug --output-wav /tmp/auxmic.wav \
  --phone-html overlays/mods/BICHENG/70-app-auxmic/root/usr/local/share/auxmic/phone.html
```

HTTP debug mode is restricted to localhost. A computer microphone can publish
the same PCM chunk requests; it checks reception and buffering, not printer
noise or the printer's sound driver. The service records only when `--output-wav`
is explicitly selected.

## Acceptance on the printer

For each firmware build, confirm the packaged module loads,
`lava:audio` can open the card, and one capture client receives phone audio.
Then check stop/disconnect, source replacement and supervisor recovery. If an old
two-process development version is running, use a clean boot into the candidate
firmware rather than mixing old and new processes.

Copy `test/check_device.py` to the printer and run it with Python 3 as a user
that can read the pairing file and open `auxmic`. For example,
`python3 /tmp/check_device.py --seconds 60` compares every generated PCM packet
with the real ALSA capture and checks the silent tail. Use `--seconds 1800` for
an endurance check. It refuses to replace an active phone session and stores
no audio recording.

Network delay, browser processing and separate source/device clocks affect the
signal. This implementation does not synchronize audio with printer motion,
compensate clock drift, guarantee uninterrupted capture across crashes, or measure
calibrated sound pressure. Those are separate capabilities, not prerequisites for
exposing a usable microphone device.
