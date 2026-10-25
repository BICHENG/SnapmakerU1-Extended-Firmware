# Auxmic

The printer has no microphone of its own.  This mod makes a phone into one:
the phone's microphone arrives as a sound card that the machine can record
from like any other device.

The point of the card is to hear the machine.  A fan curve says how fast a
fan runs; it cannot say how loud the result is.  Feeding the noise back in
through a capture device is what makes the difference measurable: the
recording is taken on the printer, so it is timed by the same clock as the
print, and the motion noise can be told apart from the fan noise.

Nothing in the printing path changes.  The service listens on the LAN, the
phone sends its audio over the network, and the printer plays that audio
into a loopback card.  A program that wants the phone's microphone opens
the card.

## How it works

Three pieces:

| Piece | Where | What it does |
| --- | --- | --- |
| `auxmic-transport.py` | mod, runs as `lava` | Serves the phone page over https, holds the pairing code, hands the audio to the device half |
| `auxmic-pacer.py` | mod, runs as root | Holds the loopback card and feeds it the audio that arrives on the link |
| `snd-aloop.ko` | firmware | The loopback card itself |

The transport and the pacer talk over a local TCP socket.  The audio on
that socket is raw PCM: 48 kHz, mono, signed 16-bit little endian, no
headers.  Both ends know the format, so the byte count is the sample count
times two.

The pacer opens `hw:Loopback,0` once and keeps it open.  The phone coming
and going changes what the card carries, not the card itself: sound while a
phone is sending, silence otherwise.  A program reading the card never sees
a device error, an underrun, or a card that was reopened underneath it.

The link is the source.  While the phone is connected the audio is live; when
the phone goes away the card turns to silence on its own, with no timeout
and no cleanup step.

## Using it

The firmware config page shows the address of the phone page and the pairing
code:

```text
https://<printer-address>:9443/#pair=<pairing code>
```

Open that address on the phone.  The page takes the pairing code from the
address, so there is nothing to type.  The phone asks for the microphone,
and the audio starts.

The browser only hands out the microphone on a secure origin, so the page
has to come over https.  The certificate is the printer's own, so the phone
shows a warning the first time and has to be told to continue.  Installing
the certificate on the phone instead is also fine.

The page has a start button and a stop button.  Stopping ends the session
and the card goes quiet.  Leaving the page ends the session too, so a
phone that loses its connection does not leave the printer making noise.

## Recording from it

The card is a normal ALSA capture device.  The alias `auxmic` opens it:

```sh
ffmpeg -f alsa -i auxmic -t 10 -ar 48000 -ac 2 phone.wav
```

The card fixes its format when the first side opens it, and the pacer opens
the playback side in 48 kHz stereo signed 16-bit, so a consumer asks for the
same.  The alias is a plug, so a consumer that asks for another rate or
another channel count still gets the audio.

`hw:Loopback,1` is the same device without the alias.

## The service

```sh
/etc/init.d/S99auxmic start
/etc/init.d/S99auxmic stop
/etc/init.d/S99auxmic restart
/etc/init.d/S99auxmic status
```

`status` prints the address of the page and the pairing code.

The logs are in `/userdata/auxmic/`, the pairing code and the certificate
are in `/home/lava/printer_data/config/extended/auxmic/`, and the status of
the device half is in `/run/auxmic/pacer.json`.

The service loads the loopback module itself.  The module belongs to the
firmware, not to this mod, so nothing happens until the mod is started.

The certificate is made again when it is close to expiring or when the
printer's address has changed.  The printer has no mDNS, so the phone can
only reach it by address, and a certificate that does not name that address
is one the phone will not open.  After the address changes, restarting the
service is what makes the page work again.

## What it needs

The loopback module has to be in the firmware.  It is built by the firmware
build:

```text
overlays/firmware-extended/10-patch-kernel-modules/scripts/01_compile_kernel_modules.sh
```

and lands in `/lib/modules/snd-aloop.ko`.  The mod loads it at boot.

## Checking it

The card shows up like any other:

```sh
cat /proc/asound/cards
```

The service reports what is feeding the card:

```sh
curl -k https://<printer-address>:9443/v1/auxmic/source
```

The answer names the source that is live, the format, the frames counted so
far, the bytes that had to be dropped and whether the link to the device half
is up.  A phone that is streaming shows a live source and a rising frame
count; a phone that is gone shows the card carrying silence.  The printer's
own settings page shows the same numbers, together with the address of the
phone page and the pairing code.

## Limits

- The phone has to accept the printer's certificate once.
- The card holds one format.  A consumer that asks for something else than
  48 kHz stereo signed 16-bit may be refused by the card; the `auxmic`
  alias converts what it can.
- The service holds one phone at a time.  A second phone takes over the
  session and the first one goes quiet.
- The audio is not recorded.  The service keeps the pairing code, and the
  session lives in memory for as long as the process does.
