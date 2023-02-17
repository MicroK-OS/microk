#
# File: tools/linux/osdep.mk
#
# This describes build options when making the UDI tools.
#

#
# $Copyright udi_reference:
# 
# 
#    Copyright (c) 1995-2001; Compaq Computer Corporation; Hewlett-Packard
#    Company; Interphase Corporation; The Santa Cruz Operation, Inc;
#    Software Technologies Group, Inc; and Sun Microsystems, Inc
#    (collectively, the "Copyright Holders").  All rights reserved.
# 
#    Redistribution and use in source and binary forms, with or without
#    modification, are permitted provided that the conditions are met:
# 
#            Redistributions of source code must retain the above
#            copyright notice, this list of conditions and the following
#            disclaimer.
# 
#            Redistributions in binary form must reproduce the above
#            copyright notice, this list of conditions and the following
#            disclaimers in the documentation and/or other materials
#            provided with the distribution.
# 
#            Neither the name of Project UDI nor the names of its
#            contributors may be used to endorse or promote products
#            derived from this software without specific prior written
#            permission.
# 
#    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#    "AS IS," AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#    HOLDERS OR ANY CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
#    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
#    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
#    OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
#    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
#    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#    DAMAGE.
# 
#    THIS SOFTWARE IS BASED ON SOURCE CODE PROVIDED AS A SAMPLE REFERENCE
#    IMPLEMENTATION FOR VERSION 1.01 OF THE UDI CORE SPECIFICATION AND/OR
#    RELATED UDI SPECIFICATIONS. USE OF THIS SOFTWARE DOES NOT IN AND OF
#    ITSELF CONSTITUTE CONFORMANCE WITH THIS OR ANY OTHER VERSION OF ANY
#    UDI SPECIFICATION.
# 
# 
# $
#


CC=cc
# -pedantic is annoying.
# So are -Wstrict-prototypes -Wmissing-prototypes and -Wmissing-declarations
# -Wall warns about problems in the lex/bison output. They can be ignored.
TOOLDEBUGFLAGS=-Wall -fno-omit-frame-pointer -g3 -DDEBUG -O
TOOLOPTFLAGS=-O
#CFLAGS=$(LOCALCFLAGS) $(TOOLDEBUGFLAGS)
CFLAGS=$(LOCALCFLAGS) $(TOOLOPTFLAGS)
LD=ld
LDFLAGS=-r -o -lefence
YACC=bison -y
# Caldera needs flex specified. RedHat uses flex by default.
LEX=flex
LIB_YACC=
LIB_LEX=-lfl
LIB_OBJ=-lelf
GEN_MSG_CAT=gencat

OS_UDIBUILD_OBJS=linux/linux_override.o

$(OS_UDIBUILD_OBJS): linux/linux_override.c linux/osdep.h common/global.h common/common_api.h
	$(CC) -c $(CFLAGS) -o $@ $<

