0.21.0 Release Notes
====================

Bitcoin Core version 0.21.0 is now available from:

  <https://bitcoincore.org/bin/bitcoin-core-0.21.0/>

This release includes new features, various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/bitcoin/bitcoin/issues>

To receive security and update notifications, please subscribe to:

  <https://bitcoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes in some cases), then run the
installer (on Windows) or just copy over `/Applications/Bitcoin-Qt` (on Mac)
or `bitcoind`/`bitcoin-qt` (on Linux).

Upgrading directly from a version of Bitcoin Core that has reached its EOL is
possible, but it might take some time if the data directory needs to be migrated. Old
wallet versions of Bitcoin Core are generally supported.

Compatibility
==============

Bitcoin Core is supported and extensively tested on operating systems
using the Linux kernel, macOS 10.12+, and Windows 7 and newer.  Bitcoin
Core should also work on most other Unix-like systems but is not as
frequently tested on them.  It is not recommended to use Bitcoin Core on
unsupported systems.

From Bitcoin Core 0.20.0 onwards, macOS versions earlier than 10.12 are no
longer supported. Additionally, Bitcoin Core does not yet change appearance
when macOS "dark mode" is activated.

The node's known peers are persisted to disk in a file called `peers.dat`. The
format of this file has been changed in a backwards-incompatible way in order to
accommodate the storage of Tor v3 and other BIP155 addresses. This means that if
the file is modified by 0.21.0 or newer then older versions will not be able to
read it. Those old versions, in the event of a downgrade, will log an error
message "Incorrect keysize in addrman deserialization" and will continue normal
operation as if the file was missing, creating a new empty one. (#19954, #20284)

Notable changes
===============

P2P and network changes
-----------------------

- The mempool now tracks whether transactions submitted via the wallet or RPCs
  have been successfully broadcast. Every 10-15 minutes, the node will try to
  announce unbroadcast transactions until a peer requests it via a `getdata`
  message or the transaction is removed from the mempool for other reasons.
  The node will not track the broadcast status of transactions submitted to the
  node using P2P relay. This version reduces the initial broadcast guarantees
  for wallet transactions submitted via P2P to a node running the wallet. (#18038)

- The size of the set of transactions that peers have announced and we consider
  for requests has been reduced from 100000 to 5000 (per peer), and further
  announcements will be ignored when that limit is reached. If you need to dump
  (very) large batches of transactions, exceptions can be made for trusted
  peers using the "relay" network permission. For localhost for example it can
  be enabled using the command line option `-whitelist=relay@127.0.0.1`.
  (#19988)

- This release adds support for Tor version 3 hidden services, and rumoring them
  over the network to other peers using
  [BIP155](https://github.com/bitcoin/bips/blob/master/bip-0155.mediawiki).
  Version 2 hidden services are still fully supported by Bitcoin Core, but the
  Tor network will start
  [deprecating](https://blog.torproject.org/v2-deprecation-timeline) them in the
  coming months. (#19954)

- The Tor onion service that is automatically created by setting the
  `-listenonion` configuration parameter will now be created as a Tor v3 service
  instead of Tor v2. The private key that was used for Tor v2 (if any) will be
  left untouched in the `onion_private_key` file in the data directory (see
  `-datadir`) and can be removed if not needed. Bitcoin Core will no longer
  attempt to read it. The private key for the Tor v3 service will be saved in a
  file named `onion_v3_private_key`. To use the deprecated Tor v2 service (not
  recommended), the `onion_private_key` can be copied over
  `onion_v3_private_key`, e.g.
  `cp -f onion_private_key onion_v3_private_key`. (#19954)

- The client writes a file (`anchors.dat`) at shutdown with the network addresses
  of the node’s two outbound block-relay-only peers (so called "anchors"). The
  next time the node starts, it reads this file and attempts to reconnect to those
  same two peers. This prevents an attacker from using node restarts to trigger a
  complete change in peers, which would be something they could use as part of an
  eclipse attack. (#17428)

- This release adds support for serving
  [BIP157](https://github.com/bitcoin/bips/blob/master/bip-0157.mediawiki) compact
  filters to peers on the network when enabled using
  `-blockfilterindex=1 -peerblockfilters=1`. (#16442)

- This release adds support for signets
  ([BIP325](https://github.com/bitcoin/bips/blob/master/bip-0325.mediawiki)) in
  addition to the existing mainnet, testnet, and regtest networks. Signets are
  centrally-controlled test networks, allowing them to be more predictable
  test environments than the older testnet. One public signet is maintained, and
  selectable using `-signet`. It is also possible to create personal signets.
  (#18267).

- This release implements
  [BIP339](https://github.com/bitcoin/bips/blob/master/bip-0339.mediawiki)
  wtxid relay. When negotiated, transactions are announced using their wtxid
  instead of their txid. (#18044).

- This release implements the proposed Taproot consensus rules
  ([BIP341](https://github.com/bitcoin/bips/blob/master/bip-0341.mediawiki) and
  [BIP342](https://github.com/bitcoin/bips/blob/master/bip-0342.mediawiki)),
  without activation on mainnet. Experimentation with Taproot can be done on
  signet, where its rules are already active. (#19553)

Updated RPCs
------------

- The `getpeerinfo` RPC has a new `network` field that provides the type of
  network ("ipv4", "ipv6", or "onion") that the peer connected through. (#20002)

- The `getpeerinfo` RPC now has additional `last_block` and `last_transaction`
  fields that return the UNIX epoch time of the last block and the last *valid*
  transaction received from each peer. (#19731)

- `getnetworkinfo` now returns two new fields, `connections_in` and
  `connections_out`, that provide the number of inbound and outbound peer
  connections. These new fields are in addition to the existing `connections`
  field, which returns the total number of peer connections. (#1