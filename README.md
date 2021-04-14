# Megamis Modules

![License](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)

Modules* for [VCV Rack](https://github.com/VCVRack/Rack).
(*Proleptic plural; only one right now.)

**NB**: Still under development; please do not attempt to use in a live performance situation!

## Clock Sync

The Clock Sync module is a utility module for synchronizing VCV Rack clocks with external devices. While it's **not**
able to overcome the inherent latency involved in sending signals through audio and MIDI interfaces to synchronize start
and stop signals, it can align an external clock so that external beat gates arrive at the same time as internal clock.
