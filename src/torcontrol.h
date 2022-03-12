// Copyright (c) 2015-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef BITCOIN_TORCONTROL_H
#define BITCOIN_TORCONTROL_H

#include <fs.h>
#include <netaddress.h>

#include <boost/signals2/signal.hpp>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class CService;

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

void StartTorControl(CService onion_service_target);
void InterruptTorControl();
void StopTorControl();

CService DefaultOnionServiceTarget();

/** Reply from Tor, can be single or multi-line */
class TorControlReply
{
public:
    TorControlReply() { Clear(); }

    int code;
    std::vector<std::string> lines;

    void Clear()
    {
        code = 0;
        lines.clear();
    }
};

/** Low-level handling for Tor control connection.
 * Speaks the SMTP-like protocol as defined in torspec/control-spec.txt
 */
class TorControlConnection
{
public:
    typedef std::function<void(TorControlConnection&)> ConnectionCB;
    typedef std::function<void(TorControlConnection &,const TorControlReply &)> ReplyHandlerCB;

    /** Create a new TorControlConnection.
     */
    explicit TorControlConnection(struct event_base *base);
    ~TorControlConnection();

    /**
     * Connect to a Tor control port.
     * tor_control_center is address of the form host:port.
     * connected is the handler that is call