#quectools
Linux support utilities for Queclink GL200 and GL300 Asset Trackers
===================================================================

All code is in the public domain. No warranties or support, whatsoever.

If you lack documentation try the following searches for PDF documents:

https://www.google.com/search?q=queclink+tracker+air+interface+protocol
https://www.google.com/search?q=queclink+sms+protocol
https://www.google.com/search?q=queclink+user+manual

Be a bit creative when looking at the search results and you will
find the proper documentation for your device.

Main differences between GL200 and GL300

GL200  - external GPS antenna connector
       - digital output (command controlled)
       - safe flight manager configuration command (airplane mode)
       - dim blue LEDs (not visible even in cloudy daylight)
       - somewhat difficult to use user button
       - SIM slot is somewhat standard (push to insert/remove SIM)
       - GPS module uses GPS only (according to Queclink web site)

GL300  - configurable vibration feedback
       - temperature alarm configuration command
       - GPS jamming detection configuration command
       - bright and colored LEDs (read, green and blue)
       - easy to use user button
       - SIM is inserted on a tray that is secured with two screws
       - GPS module uses GPS and GLONASS (according to Queclink web site)

If it is not for the minimalistic differences in the dimensions I would
suggest to use the GL200 for fixed installations and the GL300 for
mobile adhoc use (includes button usage by human beings).


queccom
=======

Serial line communication tool for Queclink GL200 and GL300.
Invoke without parameters for some short help text.

You will need an interface cable, either the "Queclink Data Cable M"
(if you can get one) or one soldered by yourself using a
PL2303 3.3V TTL serial cable and a 10pin mini USB connector
(soldering this one is real fun). Connect Vcc, GND, TxD and RxD
of the PL2303 cable to the 10pin Connector (TxD to RxD and vice
versa), make sure the PL2303 TxD voltage is not too high
(theoretical maximum is 3.6V, but better make it 3.3V or lower).
Personally I had to use a 470 Ohm resistor between PL2303 TxD
and GND to bring the TxD voltage down to an acceptable value.

In Germany you can find a proper PL2303HX cable at amazon.de and
conrad.de has the 10pin mini USB connector as well as other
stuff possibly required. Assume at least 50% losses when working
on the 10pin mini USB connector so order more than one!

If you refrain from microscopic soldering you can find a 10pin
mini USB connector at readymaderc.com (USA) which has a PCB and housing.

If you brick your device with a home made cable that's your problem.
Check any home made cable before use!

Pinout of the Queclink 10pin mini USB connector (looking at the device):

  2 4 6 8 10
-------------
\ o o o o o /
 |o o o o o|
  ---------
  1 3 5 7 9

2       4.75-5.25V Vchg (USB Vcc)
4       unknown (USB Data-)
6       unknown (USB Data+)
8       3.6-4.2V Vextbat (USB ID)
10      GND (USB GND)

1       RxD 3.3V (9600/8N1)
3       TxD 3.3V (9600/8N1)
5       NSW Button (activate=connect to GND, 0-0.8V, inact. open to 32V)
7       Ignition Detection 8-32V
9       Open Collector Output 32V/0.15A (only GL200)

Note that the pinout is taken from Queclink documentation as far as possible
and everything not documented by Queclink was measured on real devices.

The neat thing about this connector is that you can use any stock USB
cable with a mini USB B plug to charge the device. And if you dissect
a stock 1:1 5pin mini USB extension cable you can connect to an external
battery without being forced to do microscopic soldering.

The configuration file format mostly adheres to the Queclink command format
with the following exceptions:

AT+QSS is not included in the configuration file as it is redundant with
respect to AT+GTBSI and AT+GTSRI which are more complete.
AT+GT<cmd>=<password>,  becomes  AT+GT<cmd>:  in the configuration file.
The new password field of AT+GTCFG is always cleared on file read or write.

The reason for this is to prevent accidental password changes, the tool has
a password change mode.
And if you don't use stored commands the configuration file will not contain
the device password.

In terminal mode you can issue any AT+GT<cmd>=... command as well as
the following command type to retrieve data: AT+GT<cmd>?"<password>"

Furthermore the following undocumented commands are available:

AT+ESLP=0       slow clock disable (hmm, doesn't seem to make any difference)
AT+ESLP=1       slow clock enable  (hmm, doesn't seem to make any difference)
AT+CSUB         retrieve device type and version
AT+EGMR=0,7     retrieve imei


queccli
=======

Interactive communication tool via GPRS for Queclink GL200 and GL300.

If you can't use the serial communication you may configure the
device via GPRS assuming that you have GPRS configuration set up
first by SMS.

Invocation: queccli -p <port>

<port> - the listening port


quecd
=====

Interfacing daemon between Queclink GL200/GL300 and Traccar (www.traccar.org)
with logging facility (GPRS connection).

Though Traccar has basic Queclink support the support is insufficient
(no buffered message handling, no multiple position message handling,
no logging of non gps position reports, e.g. cell lists). This daemon
helps out and sends 'understandable' input to Traccar.

Invocation: quecd -p <port> [-t <traccar>] [-l <logfile>] [-f]

-f        - don't daemonize and print debug output
<port>    - the listening port
<traccar> - the traccar listening port, default is 5004
<logfile> - the file the messages are logged to, default is /var/log/quecd.log

