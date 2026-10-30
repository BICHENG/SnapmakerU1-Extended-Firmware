---
title: Phone Microphone
---

# Phone Microphone

The Auxmic mod lets recording applications use a phone microphone through an
ALSA capture device named `auxmic`. The phone sends audio over the local network.
The service does not control fans, motors or Klipper.

## Connect a phone

1. Connect the phone to the printer's network.
2. Open **Firmware Config → Phone Microphone** and copy the phone page URL.
3. Open that HTTPS URL on the phone. The URL includes the pairing code.
4. Accept the printer's certificate if the browser allows it, or install/trust
   the certificate according to the phone's requirements.
5. Press **Start** and allow microphone access. Press **Stop** when finished.

Only one source is active at a time. Starting a replacement session disconnects
the previous source. Allow about one second of buffering before audio starts.
Loss of the phone connection produces silence after queued
audio drains; leaving the page may send a stop request, but delivery is not
guaranteed. Already buffered device audio can remain briefly after Stop.

## Record

Use a recording application that supports ALSA. For example:

```sh
ffmpeg -f alsa -ar 48000 -ac 2 -i auxmic -t 10 phone.wav
```

The device carries 48 kHz stereo S16_LE, with the phone's mono signal in both
channels. The `auxmic` alias supports ALSA format conversion. Its raw capture
endpoint is `hw:Loopback,1,0`; use one direct capture application at a time.

Recordings can help compare printer sounds with the same phone, placement and
recording settings. They are not calibrated decibel measurements or synchronized
motion recordings. Network gaps and clock drift can add silence or drop samples.

## Troubleshooting

- **Microphone access is unavailable:** use the HTTPS address and check the
  browser's microphone permissions and certificate trust.
- **Pairing fails:** copy the current URL/code from Firmware Config.
- **No audio:** run `/etc/init.d/S99auxmic status`, confirm the phone is streaming,
  and check that another application is not holding the capture endpoint.
- **Address changed:** restart Phone Microphone from Firmware Config to refresh
  the certificate, then copy the new URL.
- **Capture stops after a service failure:** reopen the recording device once
  the service has restarted.

The service stores its pairing code and certificate in
`/home/lava/printer_data/config/extended/auxmic/`. Operational errors are in syslog.
For implementation details and local checks, see the
[Auxmic mod README](../overlays/mods/BICHENG/70-app-auxmic/README.md).
