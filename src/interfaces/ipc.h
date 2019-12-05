// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INTERFACES_IPC_H
#define BITCOIN_INTERFACES_IPC_H

#include <functional>
#include <memory>
#include <typeindex>

namespace ipc {
struct Context;
} // namespace ipc

namespace interfaces {
class Init;

//! Interface providing access to interprocess-communication (IPC)
//! functionality. The IPC implementation is responsible for establishing
//! connections between a controlling process and a process being controlled.
//! When a connection is established, the process being controlled returns an
//! interfaces::Init pointer to the controlling process, which the controlling
//! process can use to get access to other interfaces and functionality.
//!
//! When spawning a new process, the steps are:
//!
//! 1. The controlling process calls interfaces::Ipc::spawnProcess(), which
//!    calls ipc::Process::spawn(), which spawns a new process and returns a
//!    socketpair file descriptor for communicating with it.
//!    interfaces::Ipc::spawnProcess() then calls ipc::Protocol::connect()
//!    passing the socketpair descriptor, which returns a local proxy
//!    interf