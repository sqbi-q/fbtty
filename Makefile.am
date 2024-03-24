bin_PROGRAMS = fbtty
fbtty_SOURCES = fbtty.c
AM_LDFLAGS = -lm

scriptsdir = $(prefix)/lib
scripts_SCRIPTS = libs/terminal_oper.sh

clean-local:
	@rm config.status configure config.log
	@rm Makefile
	@rm -r autom4te.cache/
	@rm aclocal.m4
	@rm compile install-sh depcomp missing Makefile.in
