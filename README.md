# NAME

mxkbd - modular X key bind daemon - daemon to add, list, remove, and set keybinds and keybind modes

# SYNOPSIS

**mxkbd** [*OPTION*]

# DESCRIPTION

**mxkbd** is a server or daemon that allows commands be bound to key sequences (keybinds) within modes. When the key sequence within the grabbed keybind mode is pressed its (the keybind\'s) command will execute. Many modes can exist at one time but only the mode currently set to be the grabbed mode will have its keybinds watched for. Furthermore, only one mode can be set to be the grabbed mode at a time, however, the grabbed mode can be switched out to any other mode at any time as requested. By default new keybinds will be added and removed from the mode set to be the selected mode. This is to make it possible to add keybinds to one mode with key sequences of another mode. Like the grabbed mode only one mode can be set to be the selected mode. **mxkbd** needs a client program to send the appropriate requests to add, remove, list and set keybinds and modes to this daemon\'s socket file. One such client is [https://github.com/ZakharEl/mxkbc](https://github.com/ZakharEl/mxkbc).

# EXAMPLE

![an example with the client mxkbc](media/key-grabbing-example.gif)
Using the client **mxkbc** and pressing i followed by t to open alacritty in this example.

# INSTALLATION

Open a terminal and run
```sh
git clone https://github.com/ZakharEl/mxkbd
```
. Then **cd** into the clone directory and run
```sh
sudo make install
```

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

Currently the grabbed keys do not set back to the beginning of the currently grabbed mode's keybinds when a key that is not any grabbed keybind's next segment is pressed. This is being considered as a feature as it would be pretty nifty as to not have to retype a lengthy keybind if the wrong key is pressed. The keybinds do reset if other keybind's next segment but not their own is pressed though.

# TO DO

- add option to use inet in place of socket file
- possibly add option to have keybinds registered on 1 device to trigger their command to be run on another device
- add option to set default_keybind_mode, grabbed_keybind_mode and selected_keybind_mode to null, not delete them but set them to null
- add option to set custom seperator for keybind_bind seq to something other than '+'
- add option to have the keybinds reset to their beginning when something other than the next keybind portion is not pressed
- add functionality to have keybinds registered not only on keypress events but also key release events.
- add functionality to list currently grabbed keybinds
- add functionality to list the already pressed keys of a keybind
- add functionality to list the not yet pressed keys of a keybind

# AUTHORS

Written by Zachary Schlitt \<ztschlitt\@gmail.com>

# LICENSE

GPL 3.0
