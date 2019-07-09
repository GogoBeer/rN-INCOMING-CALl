#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

""" Demonstration of eBPF limitations and the effect on USDT with the
    net:inbound_message and net:outbound_message tracepoints. """

# This script shows a limitation of eBPF when data larger than 32kb is passed to
# user-space. It uses BCC (https://github.com/iovisor/bcc) to load a sandboxed
# eBPF program into the Linux kernel (root privileges are required). The eBPF
# program attaches to two statically defined tracepoints. The tracepoint
# 'net:inbound_message' is called when a new P2P message is received, and
# 'net:outbound_message' is called on outbound P2P messages. The eBPF program
# submits the P2P messages to this script via a BPF ring buffer. The submitted
# messages are printed.

# eBPF Limitations:
#
# Bitcoin P2P messages can be larger than 32kb (e.g. tx, block, ...). The eBPF
# VM's stack is limited to 512 bytes, and we can't allocate more than about 32kb
# for a P2P message in the eBPF VM. The message data is cut off when the message
# is larger than MAX_MSG_DATA_LENGTH (see definition below). This can be detected
# in user-space by comparing the data length to the message length variable. The
# message is cut off when the data length is smaller than the message length.
# A warning is included with the printed message data.
#
# Data is submitted to user-space (i.e. to this script) via a ring buffer. The
