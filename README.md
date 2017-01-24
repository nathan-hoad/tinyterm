miniterm
========

Fork of https://github.com/nathan-hoad/tinyterm that adds functionality to act as a replacement for urxvt.

- shortcuts for increasing/decreasing font size and copying/pasting
- runs against vte3 for true color support
- runs a single instance to save memory, like urxvtd/urxvtc
- parses a config file at XDG\_CONFIG\_HOME/miniterm/

Dependencies
------------

- glib2
- gtk3
- vte3 (2.91+)
