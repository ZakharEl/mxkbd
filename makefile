LIBS:=

.PHONY: all install uninstall
all: exe/mxkbd

exe/%: src/%.cpp
	g++ -pedantic -Wall -Wextra $(LIBS) -g $^ -o $@

exe/mxkbd: LIBS:=-lX11 -lxcb -lxcb-keysyms -lxcb-util

install: exe/mxkbd
	chmod 0755 exe/mxkbd
	chmod 0644 doc/mxkbd.1
	cp exe/mxkbd /usr/bin
	cp doc/mxkbd.1 /usr/share/man/man1

uninstall:
	rm /usr/bin/mxkbd
	rm /usr/share/man/man1/mxkbd.1
