#   BSD LICENSE
#
#   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Make gobbler --  a packet gobbler
# This makefile was based on the dpdk example makefile for the l2forward
# application and thus the above BSD copyright notice is included

# bloody dpdk pulls us out of this directory and puts us in unknown space so we
# are forced to code things like this rather than the -L/-l standard:
libgadget = $(PWD)/../lib/libgadget.a
libjsmn = $(PWD)/../lib/jsmn/libjsmn.a

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overriden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = gobbler

# unit test binary names for build/clean
test_bins = config_test

# generates a version string based on the git commit and makes it available 
# at compile time
VERSION = $(shell junk=$$( (git log -n 1 2>/dev/null || echo Commit non-git-build ) | awk '/^[Cc]ommit / { cid=$$NF; exit(0); } END { printf( "%s\n", cid ) }' );\
	cid="$${junk%%\|*}";\
	tag=$$( git tag -l --contains $$cid 2>/dev/null | head -1 );\
	mod=$$( git status 2>/dev/null | awk '/modified:/ { m="+++" } END { print m }' );\
	printf "%s%s%s\n" "$$mod" "$$cid.$$tag"; )


# all source are referenced via SRCS-y (including libs)
SRCS-y := gobbler.c crack_args.c config.c init.c tools.c  lib_candidates.c $(libgadget) $(libjsmn)

CFLAGS += -O3 -g
CFLAGS += $(WERROR_FLAGS) -I $(PWD)/../lib/ -I $(RTE_SDK)
EXTRA_CFLAGS += -g -Wfatal-errors -DVERSION='"$(VERSION)"' -DIPC_VIA_V6=1

include $(RTE_SDK)/mk/rte.extapp.mk

clean:
	rm -rf build
	rm -f $(test_bins)

verify:
	echo "$(VERSION)"

# ----- readme -- assumes {X}fm is installed 
README: README.xfm
	../../mk_readme.ksh

# ----- unit tests -----------------
all_tests:	$(test_bins)
	@echo "all tests made"

config_test: config_test.c config.c crack_args.c
	gcc $(CFLAGS) -DVERSION='"v1.0"' -o config_test -g -I ../lib config_test.c -L ../lib/ -lgadget -L ../lib/jsmn -ljsmn

tool_test: tool_test.c tools.c
	gcc  $(CFLAGS) -DTEST_BUILD=1 -DVERSION='"v1.0"' -o tool_test -g -I ../lib tool_test.c -L ../lib/ -lgadget -L ../lib/jsmn -ljsmn
