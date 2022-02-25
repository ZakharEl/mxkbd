# NAME

mxkbd - modular X key bind daemon - daemon to add, list, remove, and set keybinds and keybind modes

# SYNOPSIS

**mxkbd** \[*OPTION*\]

# DESCRIPTION

**mxkbd** is a server or daemon that allows commands be bound to key sequences (keybinds) within modes. When the key sequence within the grabbed keybind mode is pressed its (the keybind\'s) command will execute. Many modes can exist at one time but only the mode currently set to be the grabbed mode will have its keybinds watched for. Furthermore, only one mode can be set to be the grabbed mode at a time, however, the grabbed mode can be switched out to any other mode at any time as requested. By default new keybinds will be added and removed from the mode set to be the selected mode. This is to make it possible to add keybinds to one mode with key sequences of another mode. Like the grabbed mode only one mode can be set to be the selected mode. **mxkbd** needs a client program to send the appropriate requests to add, remove, list and set keybinds and modes to this daemon\'s socket file

# OPTIONS

**-S,--socket** *PATH*
Set this daemon's socket file to *PATH.* Default socket file path is \~/.config/mxkbd/mxkbd.socket

**-d,--default** *MODE*
Set this daemon's default mode to *MODE.*

**-g,--grabbed** *MODE*
Set this daemon's grabbed mode to *MODE.*

**-s,--selected** *MODE*
Set this daemon's selected mode to *MODE.*

**-D,--deletefile**
if set and socket file exists but is not an actual socket file then delete the file and recreate it as a socket, otherwise exit

# NOTE

Actual keybind functionality is soon to be added. Can current add, set, list and remove keybinds properties but not have the system actually register the keys.

# TO DO

- obviously add keybind functionality
- add option to use inet in place of socket file
- possibly add option to have keybinds registered on 1 device to trigger their command to be run on another device

# AUTHORS

Written by Zachary Schlitt \<ztschlitt\@gmail.com>

# LICENSE

GPL 3.0