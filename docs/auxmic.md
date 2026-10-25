---
title: Phone Microphone
---

# Phone Microphone

The printer has no microphone of its own, so a phone can be one.  This
feature turns the phone's microphone into a sound card on the printer: the
phone sends its audio over the LAN, and a program on the printer records it
the way it would record from any other capture device.

The use for it is measuring the machine.  A printer that is being tuned to
run quieter has no way to show whether a change made it quieter, because a
fan curve states a duty and not the noise that comes out of it.  With a
phone feeding a capture device, the recording is taken on the printer, so it
shares the clock of the print, and the motion noise can be told apart from
the fan noise.  The recording can also be fed to a denoising algorithm and
compared with the raw one.

The printer's own printing path is untouched.  Nothing is written to the
motion system, and the service does not talk to Klipper.

## What you need

- A phone on the same network as the printer.
- A browser on the phone.  The page asks for the microphone, so the browser
  has to allow it.
- The printer's address, which the firmware config page shows.

## Start it

Open **Firmware Config**.  The **Phone Microphone** section shows the
service state, the address of the page, and the pairing code:

```text
https://<printer-address>:9443/#pair=<pairing code>
```

Open that address on the phone.  The page takes the pairing code from the
address, so there is nothing to type.

The browser only gives a page the microphone on a secure origin, so the
page has to come over https.  The printer serves the page with a
certificate of its own, so the phone shows a warning the first time.  Tap
through it, or install the certificate on the phone:

```sh
/etc/init.d/S99auxmic status
```

The status prints the address of the page and the pairing code.

## Use it

Press **Start** on the phone.  The page asks for the microphone, and the
audio starts flowing to the printer.  **Stop** ends it.  Leaving the page
ends it too, so a phone that walks away does not leave the printer making
noise.

One phone at a time.  A second phone takes the session over and the first
one goes quiet.

## Record from it

The printer sees a normal ALSA capture device called `auxmic`:

```sh
ffmpeg -f alsa -i auxmic -t 10 -ar 48000 -ac 2 phone.wav
```

The device fixes its format when the first side opens it, and the service
opens the playback side in 48 kHz stereo signed 16-bit, so a consumer asks
for the same.  The alias converts what it can, so another rate or another
channel count still works.

The same device without the alias is `hw:Loopback,1`.

## When something is wrong

**The page says the browser cannot record.**
The page did not come over https, so the browser did not offer the
microphone.  Use the address from the firmware config page, not `http://`.

**The phone shows a certificate warning.**
Expected on the first visit.  The certificate is the printer's own.  Tap
through it, or install the certificate on the phone.

**The page says the pairing code is wrong.**
The code changed, which happens when the service is restarted with a new
configuration directory.  Take the new one from the firmware config page.

**Nothing is recorded.**
Check that the service is running (`/etc/init.d/S99auxmic status`) and that
the phone shows **Stop**, not **Start**: a phone that is connected but not
sending audio records silence, by design.

**The page stopped working after the printer's address changed.**
The certificate names the address the phone uses, so it is made again when
the address changes.  Restart the service from the firmware config page
(**Actions → Services → Restart Phone Microphone**) and open the new
address.
