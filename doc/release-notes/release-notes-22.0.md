22.0 Release Notes
==================

Bitcoin Core version 22.0 is now available from:

  <https://bitcoincore.org/bin/bitcoin-core-22.0/>

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
using the Linux kernel, macOS 10.14+, and Windows 7 and newer.  Bitcoin
Core should also work on most other Unix-like systems but is not as
frequently tested on them.  It is not recommended to use Bitcoin Core on
unsupported systems.

From Bitcoin Core 22.0 onwards, macOS versions earlier than 10.14 are no longer supported.

Notable changes
===============

P2P and network changes
-----------------------
- Added support for running Bitcoin Core as an
  [I2P (Invisible Internet Project)](https://en.wikipedia.org/wiki/I2P) service
  and connect to such services. See [i2p.md](https://github.com/bitcoin/bitcoin/blob/22.x/doc/i2p.md) for details. (#20685)
- This release removes support for Tor version 2 hidden services in favor of Tor
  v3 only, as the Tor network [dropped support for Tor
  v2](https://blog.torproject.org/v2-deprecation-timeline) with the release of
  Tor version 0.4.6.  Henceforth, Bitcoin Core ignores Tor v2 addresses; it
  neither rumors them over the network to other peers, nor stores them in memory
  or to `peers.dat`.  (#22050)

- Added NAT-PMP port mapping support via
  [`libnatpmp`](https://miniupnp.tuxfamily.org/libnatpmp.html). (#18077)

New and Updated RPCs
--------------------

- Due to [BIP 350](https://github.com/bitcoin/bips/blob/master/bip-0350.mediawiki)
  being implemented, behavior for all RPCs that accept addresses is changed when
  a native witness version 1 (or higher) is passed. These now require a Bech32m
  encoding instead of a Bech32 one, and Bech32m encoding will be used for such
  addresses in RPC output as well. No version 1 addresses should be created
  for mainnet until consensus rules are adopted that give them meaning
  (as will happen through [BIP 341](https://github.com/bitcoin/bips/blob/master/bip-0341.mediawiki)).
  Once that happens, Bech32m is expected to be used for them, so this shouldn't
  affect any production systems, but may be observed on other networks where such
  addresses already have meaning (like signet). (#20861)

- The `getpeerinfo` RPC returns two new boolean fields, `bip152_hb_to` and
  `bip152_hb_from`, that respectively indicate whether we selected a peer to be
  in compact blocks high-bandwidth mode or whether a peer selected us as a
  compact blocks high-bandwidth peer. High-bandwidth peers send new block
  announcements via a `cmpctblock` message rather than the usual inv/headers
  announcements. See BIP 152 for more details. (#19776)

- `getpeerinfo` no longer returns the following fields: `addnode`, `banscore`,
  and `whitelisted`, which were previously deprecated in 0.21. Instead of
  `addnode`, the `connection_type` field returns manual. Instead of
  `whitelisted`, the `permissions` field indicates if the peer has special
  privileges. The `banscore` field has simply been removed. (#20755)

- The following RPCs:  `gettxout`, `getrawtransaction`, `decoderawtransaction`,
  `decodescript`, `gettransaction`, and REST endpoints: `/rest/tx`,
  `/rest/getutxos`, `/rest/block` deprecated the following fields (which are no
  longer returned in the responses by default): `addresses`, `reqSigs`.
  The `-deprecatedrpc=addresses` flag must be passed for these fields to be
  included in the RPC response. This flag/option will be available only for this major release, after which
  the deprecation will be removed entirely. Note that these fields are attributes of
  the `scriptPubKey` object returned in the RPC response. However, in the response
  of `decodescript` these fields are top-level attributes, and included again as attributes
  of the `scriptPubKey` object. (#20286)

- When creating a hex-encoded bitcoin transaction using the `bitcoin-tx` utility
  with the `-json` option set, the following fields: `addresses`, `reqSigs` are no longer
  returned in the tx output of the response. (#20286)

- The `listbanned` RPC now returns two new numeric fields: `ban_duration` and `time_remaining`.
  Respectively, these new fields indicate the duration of a ban and the time remaining until a ban expires,
  both in seconds. Additionally, the `ban_created` field is repositioned to come before `banned_until`. (#21602)

- The `setban` RPC can ban onion addresses again. This fixes a regression introduced in version 0.21.0. (#20852)

- The `getnodeaddresses` RPC now returns a "network" field indicating the
  network type (ipv4, ipv6, onion, or i2p) for each address.  (#21594)

- `getnodeaddresses` now also accepts a "network" argument (ipv4, ipv6, onion,
  or i2p) to return only addresses of the specified network.  (#21843)

- The `testmempoolaccept` RPC now accepts multiple transactions (still experimental at the moment,
  API may be unstable). This is intended for testing transaction packages with dependency
  relationships; it is not recommended for batch-validating independent transactions. In addition to
  mempool policy, package policies apply: the list cannot contain more than 25 transactions or have a
  total size exceeding 101K virtual bytes, and cannot conflict with (spend the same inputs as) each other or
  the mempool, even if it would be a valid BIP125 replace-by-fee. There are some known limitations to
  the accuracy of the test accept: it's possible for `testmempoolaccept` to return "allowed"=True for a
  group of transactions, but "too-long-mempool-chain" if they are actually submitted. (#20833)

- `addmultisigaddress` and `createmultisig` now support up to 20 keys for
  Segwit addresses. (#20867)

Changes to Wallet or GUI related RPCs can be found in the GUI or Wallet section below.

Build System
------------

- Release binaries are now produced using the new `guix`-based build system.
  The [/doc/release-process.md](/doc/release-process.md) document has been updated accordingly.

Files
-----

- The list of banned hosts and networks (via `setban` RPC) is now saved on disk
  in JSON format in `banlist.json` instead of `banlist.dat`. `banlist.dat` is
  only read on startup if `banlist.json` is not present. Changes are only written to the new
  `banlist.json`. A future version of Bitcoin Core may completely ignore
  `banlist.dat`. (#20966)

New settings
------------

- The `-natpmp` option has been added to use NAT-PMP to map the listening port.
  If both UPnP and NAT-PMP are enabled, a successful allocation from UPnP
  prevails over one from NAT-PMP. (#18077)

Updated settings
----------------

Changes to Wallet or GUI related settings can be found in the GUI or Wallet section below.

- Passing an invalid `-rpcauth` argument now cause bitcoind to fail to start.  (#20461)

Tools and Utilities
-------------------

- A new CLI `-addrinfo` command returns the number of addresses known to the
  node per network type (including Tor v2 versus v3) and total. This can be
  useful to see if the node knows enough addresses in a network to use options
  like `-onlynet=<network>` or to upgrade to this release of Bitcoin Core 22.0
  that supports Tor v3 only.  (#21595)

- A new `-rpcwaittimeout` argument to `bitcoin-cli` sets the timeout
  in seconds to use with `-rpcwait`. If the timeout expires,
  `bitcoin-cli` will report a failure. (#21056)

Wallet
------

- External signers such as hardware wallets can now be used through the new RPC methods `enumeratesigners` and `displayaddress`. Support is also added to the `send` RPC call. This feature is experimental. See [external-signer.md](https://github.com/bitcoin/bitcoin/blob/22.x/doc/external-signer.md) for details. (#16546)

- A new `listdescriptors` RPC is available to inspect the contents of descriptor-enabled wallets.
  The RPC returns public versions of all imported descriptors, including their timestamp and flags.
  For ranged descriptors, it also returns the range boundaries and the next index to generate addresses from. (#20226)

- The `bumpfee` RPC is not available with wallets that have private keys
  disabled. `psbtbumpfee` can be used instead. (#20891)

- The `fundrawtransaction`, `send` and `walletcreatefundedpsbt` RPCs now support an `include_unsafe` option
  that when `true` allows using unsafe inputs to fund the transaction.
  Note that the resulting transaction may become invalid if one of the unsafe inputs disappears.
  If that happens, the transaction must be funded with different inputs and republished. (#21359)

- We now support up to 20 keys in `multi()` and `sortedmulti()` descriptors
  under `wsh()`. (#20867)

- Taproot descriptors can be imported into the wallet only after activation has occurred on the network (e.g. mainnet, testnet, signet) in use. See [descriptors.md](https://github.com/bitcoin/bitcoin/blob/22.x/doc/descriptors.md) for supported descriptors.

GUI changes
-----------

- External signers such as hardware wallets can now be used. These require an external tool such as [HWI](https://github.com/bitcoin-core/HWI) to be installed and configured under Options -> Wallet. When creating a new wallet a new option "External signer" will appear in the dialog. If the device is detected, its name is suggested as the wallet name. The watch-only keys are then automatically imported. Receive addresses can be verified on the device. The send dialog will automatically use the connected device. This feature is experimental and the UI may freeze for a few seconds when performing these actions.

Low-level changes
=================

RPC
---

- The RPC server can process a limited number of simultaneous RPC requests.
  Previously, if this limit was exceeded, the RPC server would respond with
  [status code 500 (`HTTP_INTERNAL_SERVER_ERROR`)](https://en.wikipedia.org/wiki/List_of_HTTP_status_codes#5xx_server_errors).
  Now it returns status code 503 (`HTTP_SERVICE_UNAVAILABLE`). (#18335)

- Error codes have been updated to be more accurate for the following error cases (#18466):
  - `signmessage` now returns RPC_INVALID_ADDRESS_OR_KEY (-5) if the
    passed address is invalid. Previously returned RPC_TYPE_ERROR (-3).
  - `verifymessage` now returns RPC_INVALID_ADDRESS_OR_KEY (-5) if the
    passed address is invalid. Previously returned RPC_TYPE_ERROR (-3).
  - `verifymessage` now returns RPC_TYPE_ERROR (-3) if the passed signature
    is malformed. Previously returned RPC_INVALID_ADDRESS_OR_KEY (-5).

Tests
-----

22.0 change log
===============

A detailed list of changes in this version follows. To keep the list to a manageable length, small refactors and typo fixes are not included, and similar changes are sometimes condensed into one line.

### Consensus
- bitcoin/bitcoin#19438 Introduce deploymentstatus (ajtowns)
- bitcoin/bitcoin#20207 Follow-up extra comments on taproot code and tests (sipa)
- bitcoin/bitcoin#21330 Deal with missing data in signature hashes more consistently (sipa)

### Policy
- bitcoin/bitcoin#18766 Disable fee estimation in blocksonly mode (by removing the fee estimates global) (darosior)
- bitcoin/bitcoin#20497 Add `MAX_STANDARD_SCRIPTSIG_SIZE` to policy (sanket1729)
- bitcoin/bitcoin#20611 Move `TX_MAX_STANDARD_VERSION` to policy (MarcoFalke)

### Mining
- bitcoin/bitcoin#19937, bitcoin/bitcoin#20923 Signet mining utility (ajtowns)

### Block and transaction handling
- bitcoin/bitcoin#14501 Fix possible data race when committing block files (luke-jr)
- bitcoin/bitcoin#15946 Allow maintaining the blockfilterindex when using prune (jonasschnelli)
- bitcoin/bitcoin#18710 Add local thread pool to CCheckQueue (hebasto)
- bitcoin/bitcoin#19521 Coinstats Index (fjahr)
- bitcoin/bitcoin#19806 UTXO snapshot activation (jamesob)
- bitcoin/bitcoin#19905 Remove dead CheckForkWarningConditionsOnNewFork (MarcoFalke)
- bitcoin/bitcoin#19935 Move SaltedHashers to separate file and add some new ones (achow101)
- bitcoin/bitcoin#20054 Remove confusing and useless "unexpected version" warning (MarcoFalke)
- bitcoin/bitcoin#20519 Handle rename failure in `DumpMempool(…)` by using the `RenameOver(…)` return value (practicalswift)
- bitcoin/bitcoin#20749, bitcoin/bitcoin#20750, bitcoin/bitcoin#21055, bitcoin/bitcoin#21270, bitcoin/bitcoin#21525, bitcoin/bitcoin#21391, bitcoin/bitcoin#21767, bitcoin/bitcoin#21866 Prune `g_chainman` usage (dongcarl)
- bitcoin/bitcoin#20833 rpc/validation: enable packages through testmempoolaccept (glozow)
- bitcoin/bitcoin#20834 Locks and docs in ATMP and CheckInputsFromMempoolAndCache (glozow)
- bitcoin/bitcoin#20854 Remove unnecessary try-block (amitiuttarwar)
- bitcoin/bitcoin#20868 Remove redundant check on pindex (jarolrod)
- bitcoin/bitcoin#20921 Don't try to invalidate genesis block in CChainState::InvalidateBlock (theStack)
- bitcoin/bitcoin#20972 Locks: Annotate CTxMemPool::check to require `cs_main` (dongcarl)
- bitcoin/bitcoin#21009 Remove RewindBlockIndex logic (dhruv)
- bitcoin/bitcoin#21025 Guard chainman chainstates with `cs_main` (dongcarl)
- bitcoin/bitcoin#21202 Two small clang lock annotation improvements (amitiuttarwar)
- bitcoin/bitcoin#21523 Run VerifyDB on all chainstates (jamesob)
- bitcoin/bitcoin#21573 Update libsecp256k1 subtree to latest master (sipa)
- bitcoin/bitcoin#21582, bitcoin/bitcoin#21584, bitcoin/bitcoin#21585 Fix assumeutxo crashes (MarcoFalke)
- bitcoin/bitcoin#21681 Fix ActivateSnapshot to use hardcoded nChainTx (jamesob)
- bitcoin/bitcoin#21796 index: Avoid async shutdown on init error (MarcoFalke)
- bitcoin/bitcoin#21946 Document and test lack of inherited signaling in RBF policy (ariard)
- bitcoin/bitcoin#22084 Package testmempoolaccept followups (glozow)
- bitcoin/bitcoin#22102 Remove `Warning:` from warning message printed for unknown new rules (prayank23)
- bitcoin/bitcoin#22112 Force port 0 in I2P (vasild)
- bitcoin/bitcoin#22135 CRegTestParams: Use `args` instead of `gArgs` (kiminuo)
- bitcoin/bitcoin#22146 Reject invalid coin height and output index when loading assumeutxo (MarcoFalke)
- bitcoin/bitcoin#22253 Distinguish between same tx and same-nonwitness-data tx in mempool (glozow)
- bitcoin/bitcoin#22261 Two small fixes to node broadcast logic (jnewbery)
- bitcoin/bitcoin#22415 Make `m_mempool` optional in CChainState (jamesob)
- bitcoin/bitcoin#22499 Update assumed chain params (sriramdvt)
- bitcoin/bitcoin#22589 net, doc: update I2P hardcoded seeds and docs for 22.0 (jonatack)

### P2P protocol and network code
- bitcoin/bitcoin#18077 Add NAT-PMP port forwarding support (hebasto)
- bitcoin/bitcoin#18722 addrman: improve performance by using more suitable containers (vasild)
- bitcoin/bitcoin#18819 Replace `cs_feeFilter` with simple std::atomic (MarcoFalke)
- bitcoin/bitcoin#19203 Add regression fuzz harness for CVE-2017-18350. Add FuzzedSocket (practicalswift)
- bitcoin/bitcoin#19288 fuzz: Add fuzzing harness for TorController (practicalswift)
- bitcoin/bitcoin#19415 Make DNS lookup mockable, add fuzzing harness (practicalswift)
- bitcoin/bitcoin#19509 Per-Peer Message Capture (troygiorshev)
- bitcoin/bitcoin#19763 Don't try to relay to the address' originator (vasild)
- bitcoin/bitcoin#19771 Replace enum CConnMan::NumConnections with enum class ConnectionDirection (luke-jr)
- bitcoin/bitcoin#19776 net, rpc: expose high bandwidth mode state via getpeerinfo (theStack)
- bitcoin/bitcoin#19832 Put disconnecting logs into BCLog::NET category (hebasto)
- bitcoin/bitcoin#19858 Periodically make block-relay connections and sync headers (sdaftuar)
- bitcoin/bitcoin#19884 No delay in adding fixed seeds if -dnsseed=0 and peers.dat is empty (dhruv)
- bitcoin/bitcoin#20079 Treat handshake misbehavior like unknown message (MarcoFalke)
- bitcoin/bitcoin#20138 Assume that SetCommonVersion is called at most once per peer (MarcoFalke)
- bitcoin/bitcoin#20162 p2p: declare Announcement::m_state as uint8_t, add getter/setter (jonatack)
- bitcoin/bitcoin#20197 Protect onions in AttemptToEvictConnection(), add eviction protection test coverage (jonatack)
- bitcoin/bitcoin#20210 assert `CNode::m_inbound_onion` is inbound in ctor, add getter, unit tests (jonatack)
- bitcoin/bitcoin#20228 addrman: Make addrman a top-level component (jnewbery)
- bitcoin/bitcoin#20234 Don't bind on 0.0.0.0 if binds are restricted to Tor (vasild)
- bitcoin/bitcoin#20477 Add unit testing of node eviction logic (practicalswift)
- bitcoin/bitcoin#20516 Well-defined CAddress disk serialization, and addrv2 anchors.dat (sipa)
- bitcoin/bitcoin#20557 addrman: Fix new table bucketing during unserialization (jnewbery)
- bitcoin/bitcoin#20561 Periodically clear `m_addr_known` (sdaftuar)
- bitcoin/bitcoin#20599 net processing: Tolerate sendheaders and sendcmpct messages before verack (jnewbery)
- bitcoin/bitcoin#20616 Check CJDNS address is valid (lontivero)
- bitcoin/bitcoin#20617 Remove `m_is_manual_connection` from CNodeState (ariard)
- bitcoin/bitcoin#20624 net processing: Remove nStartingHeight check from block relay (jnewbery)
- bitcoin/bitcoin#20651 Make p2p recv buffer timeout 20 minutes for all peers (jnewbery)
- bitcoin/bitcoin#20661 Only select from addrv2-capable peers for torv3 address relay (sipa)
- bitcoin/bitcoin#20685 Add I2P support using I2P SAM (vasild)
- bitcoin/bitcoin#20690 Clean up logging of outbound connection type (sdaftuar)
- bitcoin/bitcoin#20721 Move ping data to `net_processing` (jnewbery)
- bitcoin/bitcoin#20724 Cleanup of -debug=net log messages (ajtowns)
- bitcoin/bitcoin#20747 net processing: Remove dropmessagestest (jnewbery)
- bitcoin/bitcoin#20764 cli -netinfo peer connections dashboard updates 🎄 ✨ (jonatack)
- bitcoin/bitcoin#20788 add RAII socket and use it instead of bare SOCKET (vasild)
- bitcoin/bitcoin#20791 remove unused legacyWhitelisted in AcceptConnection() (jonatack)
- bitcoin/bitcoin#20816 Move RecordBytesSent() call out of `cs_vSend` lock (jnewbery)
- bitcoin/bitcoin#20845 Log to net debug in MaybeDiscourageAndDisconnect except for noban and manual peers (MarcoFalke)
- bitcoin/bitcoin#20864 Move SocketSendData lock annotation to header (MarcoFalke)
- bitcoin/bitcoin#20965 net, rpc:  return `NET_UNROUTABLE` as `not_publicly_routable`, automate helps (jonatack)
- bitcoin/bitcoin#20966 banman: save the banlist in a JSON format on disk (vasild)
- bitcoin/bitcoin#21015 Make all of `net_processing` (and some of net) use std::chrono types (dhruv)
- bitcoin/bitcoin#21029 bitcoin-cli: Correct docs (no "generatenewaddress" exists) (luke-jr)
- bitcoin/bitcoin#21148 Split orphan handling from `net_processing` into txorphanage (ajtowns)
- bitcoin/bitcoin#21162 Net Processing: Move RelayTransaction() into PeerManager (jnewbery)
- bitcoin/bitcoin#21167 make `CNode::m_inbound_onion` public, initialize explicitly (jonatack)
- bitcoin/bitcoin#21186 net/net processing: Move addr data into `net_processing` (jnewbery)
- bitcoin/bitcoin#21187 Net processing: Only call PushAddress() from `net_processing` (jnewbery)
- bitcoin/bitcoin#21198 Address outstanding review comments from PR20721 (jnewbery)
- bitcoin/bitcoin#21222 log: Clarify log message when file does not exist (MarcoFalke)
- bitcoin/bitcoin#21235 Clarify disconnect log message in ProcessGetBlockData, remove send bool (MarcoFalke)
- bitcoin/bitcoin#21236 Net processing: Extract `addr` send functionality into MaybeSendAddr() (jnewbery)
- bitcoin/bitcoin#21261 update inbound eviction protection for multiple networks, add I2P peers (jonatack)
- bitcoin/bitcoin#21328 net, refactor: pass uint16 CService::port as uint16 (jonatack)
- bitcoin/bitcoin#21387 Refactor sock to add I2P fuzz and unit tests (vasild)
- bitcoin/bitcoin#21395 Net processing: Remove unused CNodeState.address member (jnewbery)
- bitcoin/bitcoin#21407 i2p: limit the size of incoming messages (vasild)
- bitcoin/bitcoin#21506 p2p, refactor: make NetPermissionFlags an enum class (jonatack)
- bitcoin/bitcoin#21509 Don't send FEEFILTER in blocksonly mode (mzumsande)
- bitcoin/bitcoin#21560 Add Tor v3 hardcoded seeds (laanwj)
- bitcoin/bitcoin#21563 Restrict period when `cs_vNodes` mutex is locked (hebasto)
- bitcoin/bitcoin#21564 Avoid calling getnameinfo when formatting IPv4 addresses in CNetAddr::ToStringIP (practicalswift)
- bitcoin/bitcoin#21631 i2p: always check the return value of Sock::Wait() (vasild)
- bitcoin/bitcoin#21644 p2p, bugfix: use NetPermissions::HasFlag() in CConnman::Bind() (jonatack)
- bitcoin/bitcoin#21659 flag relevant Sock methods with [[nodiscard]] (vasild)
- bitcoin/bitcoin#21750 remove unnecessary check of `CNode::cs_vSend` (vasild)
- bitcoin/bitcoin#21756 Avoid calling `getnameinfo` when formatting IPv6 addresses in `CNetAddr::ToStringIP` (practicalswift)
- bitcoin/bitcoin#21775 Limit `m_block_inv_mutex` (MarcoFalke)
- bitcoin/bitcoin#21825 Add I2P hardcoded seeds (jonatack)
- bitcoin/bitcoin#21843 p2p, rpc: enable GetAddr, GetAddresses, and getnodeaddresses by network (jonatack)
- bitcoin/bitcoin#21845 net processing: Don't require locking `cs_main` before calling RelayTransactions() (jnewbery)
- bitcoin/bitcoin#21872 Sanitize message type for logging (laanwj)
- bitcoin/bitcoin#21914 Use stronger AddLocal() for our I2P address (vasild)
- bitcoin/bitcoin#21985 Return IPv6 scope id in `CNetAddr::ToStringIP()` (laanwj)
- bitcoin/bitcoin#21992 Remove -feefilter option (amadeuszpawlik)
- bitcoin/bitcoin#21996 Pass strings to NetPermissions::TryParse functions by const ref (jonatack)
- bitcoin/bitcoin#22013 ignore block-relay-only peers when skipping DNS seed (ajtowns)
- bitcoin/bitcoin#22050 Remove tor v2 support (jonatack)
- bitcoin/bitcoin#22096 AddrFetch - don't disconnect on self-announcements (mzumsande)
- bitcoin/bitcoin#22141 net processing: Remove hash and fValidatedHeaders from QueuedBlock (jnewbery)
- bitcoin/bitcoin#22144 Randomize message processing peer order (sipa)
- bitcoin/bitcoin#22147 Protect last outbound HB compact block peer (sdaftuar)
- bitcoin/bitcoin#22179 Torv2 removal followups (vasild)
- bitcoin/bitcoin#22211 Relay I2P addresses even if not reachable (by us) (vasild)
- bitcoin/bitcoin#22284 Performance improvements to ProtectEvictionCandidatesByRatio() (jonatack)
- bitcoin/bitcoin#22387 Rate limit the processing of rumoured addresses (sipa)
- bitcoin/bitcoin#22455 addrman: detect on-disk corrupted nNew and nTried during unserialization (vasild)

### Wallet
- bitcoin/bitcoin#15710 Catch `ios_base::failure` specifically (Bushstar)
- bitcoin/bitcoin#16546 External signer support - Wallet Box edition (Sjors)
- bitcoin/bitcoin#17331 Use effective values throughout coin selection (achow101)
- bitcoin/bitcoin#18418 Increase `OUTPUT_GROUP_MAX_ENTRIES` to 100 (fjahr)
- bitcoin/bitcoin#18842 Mark replaced tx to not be in the mempool anymore (MarcoFalke)
- bitcoin/bitcoin#19136 Add `parent_desc` to `getaddressinfo` (achow101)
- bitcoin/bitcoin#19137 wallettool: Add dump and createfromdump commands (achow101)
- bitcoin/bitcoin#19651 `importdescriptor`s update existing (S3RK)
- bitcoin/bitcoin#20040 Refactor OutputGroups to handle fees and spending eligibility on grouping (achow101)
- bitcoin/bitcoin#20202 Make BDB support optional (achow101)
- bitcoin/bitcoin#20226, bitcoin/bitcoin#21277, - bitcoin/bitcoin#21063 Add `listdescriptors` command (S3RK)
- bitcoin/bitcoin#20267 Disable and fix tests for when BDB is not compiled (achow101)
- bitcoin/bitcoin#20275 List all wallets in non-SQLite and non-BDB builds (ryanofsky)
- bitcoin/bitcoin#20365 wallettool: Add parameter to create descriptors wallet (S3RK)
- bitcoin/bitcoin#20403 `upgradewallet` fixes, improvements, test coverage (jonatack)
- bitcoin/bitcoin#20448 `unloadwallet`: Allow specifying `wallet_name` param matching RPC endpoint wallet (luke-jr)
- bitcoin/bitcoin#20536 Error with "Transaction too large" if the funded tx will end up being too large after signing (achow101)
- bitcoin/bitcoin#20687 Add missing check for -descriptors wallet tool option (MarcoFalke)
- bitcoin/bitcoin#20952 Add BerkeleyDB version sanity check at init time (laanwj)
- bitcoin/bitcoin#21127 Load flags before everything else (Sjors)
- bitcoin/bitcoin#21141 Add new format string placeholders for walletnotify (maayank)
- bitcoin/bitcoin#21238 A few descriptor improvements to prepare for Taproot support (sipa)
- bitcoin/bitcoin#21302 `createwallet` examples for descriptor wallets (S3RK)
- bitcoin/bitcoin#21329 descriptor wallet: Cache last hardened xpub and use in normalized descriptors (achow101)
- bitcoin/bitcoin#21365 Basic Taproot signing support for descriptor wallets (sipa)
- bitcoin/bitcoin#21417 Misc external signer improvement and HWI 2 support (Sjors)
- bitcoin/bitcoin#21467 Move external signer out of wallet module (Sjors)
- bitcoin/bitcoin#21572 Fix wrong wallet RPC context set after #21366 (ryanofsky)
- bitcoin/bitcoin#21574 Drop JSONRPCRequest constructors after #21366 (ryanofsky)
- bitcoin/bitcoin#21666 Miscellaneous external signer changes (fanquake)
- bitcoin/bitcoin#21759 Document coin selection code (glozow)
- bitcoin/bitcoin#21786 Ensure sat/vB feerates are in range (mantissa of 3) (jonatack)
- bitcoin/bitcoin#21944 Fix issues when `walletdir` is root directory (prayank23)
- bitcoin/bitcoin#22042 Replace size/weight estimate tuple with struct for named fields (instagibbs)
- bitcoin/bitcoin#22051 Basic Taproot derivation support for descriptors (sipa)
- bitcoin/bitcoin#22154 Add OutputType::BECH32M and related wallet support for fetching bech32m addresses (achow101)
- bitcoin/bitcoin#22156 Allow tr() import only when Taproot is active (achow101)
- bitcoin/bitcoin#22166 Add support for inferring tr() descriptors (sipa)
- bitcoin/bitcoin#22173 Do not load external signers wallets when unsupported (achow101)
- bitcoin/bitcoin#22308 Add missing BlockUntilSyncedToCurrentChain (MarcoFalke)
- bitcoin/bitcoin#22334 Do not spam about non-existent spk managers (S3RK)
- bitcoin/bitcoin#22379 Erase spkmans rather than setting to nullptr (achow101)
- bitcoin/bitcoin#22421 Make IsSegWitOutput return true for taproot outputs (sipa)
- bitcoin/bitcoin#22461 Change ScriptPubKeyMan::Upgrade default to True (achow101)
- bitcoin/bitcoin#22492 Reorder locks in dump