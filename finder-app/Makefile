#Write a Makefile which includes:
#
#    A default target which builds the “writer” application
#
#    A clean target which removes the “writer” application and all .o files
#
#    Support for cross-compilation.
#	You should be able to generate an application for the native build platform
#	when GNU make variable CROSS_COMPILE is not specified on the make command line.
#	When CROSS_COMPILE is specified with aarch64-none-linux-gnu- (note the trailing -)
#	your makefile should compile successfully using the cross compiler installed in step 1.

all: 	writer

writer:	writer.c
ifndef CROSS_COMPILE
	gcc -o writer writer.c
else
ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
	aarch64-none-linux-gnu-gcc -o writer writer.c
else
	@echo "Unknown target set"
endif
endif

clean:
	@-rm -f writer
	@-rm -f *.o
