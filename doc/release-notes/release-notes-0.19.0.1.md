Bitcoin Core version 0.19.0.1 is now available from:

  <https://bitcoincore.org/bin/bitcoin-core-0.19.0.1/>

This release includes new features, various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/bitcoin/bitcoin/issues>

To receive security and update notifications, please subscribe to:

  <https://bitcoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over `/Applications/Bitcoin-Qt` (on Mac)
or `bitcoind`/`bitcoin-qt` (on Linux).

Upgrading directly from a version of Bitcoin Core that has reached its EOL is
possible, but might take some time if the datadir needs to be migrated.  Old
wallet versions of Bitcoin Core are generally supported.

Compatibility
==============

Bitcoin Core is supported and extensively tested on operating systems using
the Linux kernel, macOS 10.10+, and Windows 7 and newer. It is not recommended
to use Bitcoin Core on unsupported systems.

Bitcoin Core should also work on most other Unix-like systems but is not
as frequently tested on them.

From 0.17.0 onwards, macOS <10.10 is no longer supported. 0.17.0 is
built using Qt 5.9.x, which doesn't support versions of macOS older than
10.10. Additionally, Bitcoin Core does not yet change appearance when
macOS "dark mode" is activated.

Users running macOS Catalina may need to "right-click" and then choose "Open"
to open the Bitcoin Core .dmg. This is due to new signing requirements
imposed by Apple, which the Bitcoin Core project does not yet adhere too.

Notable changes
===============

New user documentation
----------------------

- [Reduce memory](https://github.com/bitcoin/bitcoin/blob/master/doc/reduce-memory.md)
  suggests configuration tweaks for running Bitcoin Core on systems with
  limited memory. (#16339)

New RPCs
--------

- `getbalances` returns an object with all balances (`mine`,
  `untrusted_pending` and `immature`). Please refer to the RPC help of
  `getbalances` for details. The new RPC is intended to replace
  `getbalance`, `getunconfirmedbalance`, and the balance fields in
  `getwalletinfo`.  These old calls and fields may be removed in a
  future version. (#15930, #16239)

- `setwalletflag` sets and unsets wallet flags that enable or disable
  features specific to that existing wallet, such as the new
  `avoid_reuse` feature documented elsewhere in these release notes.
  (#13756)

- `getblockfilter` gets the BIP158 filter for the specified block.  This
  RPC is only enabled if block filters have been created using the
  `-blockfilterindex` configuration option. (#14121)

New settings
------------

- `-blockfilterindex` enables the creation of BIP158 block filters for
  the entire blockchain.  Filters will be created in the background and
  currently use about 4 GiB of space.  Note: this version of Bitcoin
  Core does not serve block filters over the P2P network, although the
  local user may obtain block filters using the `getblockfilter` RPC.
  (#14121)

Updated settings
----------------

- `whitebind` and `whitelist` now accept a list of permissions to
  provide peers connecting using the indicated interfaces or IP
  addresses.  If no permissions are specified with an address or CIDR
  network, the implicit default permissions are the same as previous
  releases.  See the `bitcoind -help` output for these two options for
  details about the available permissions. (#16248)

- Users setting custom `dbcache` values can increase their setting slightly
  without using any more real memory.  Recent changes reduced the memory use
  by about 9% and made chainstate accounting more accurate (it was underestimating
  the use of memory before).  For example, if you set a value of "450" before, you
  may now set a value of "500" to use about the same real amount of memory. (#16957)


Updated RPCs
------------

Note: some low-level RPC changes mainly useful for testing are described in the
Low-level Changes section below.

- `sendmany` no longer has a `minconf` argument.  This argument was not
  well-specified and would lead to RPC errors even when the wallet's
  coin selection succeeded.  Users who want to influence coin selection
  can use the existing `-spendzeroconfchange`, `-limitancestorcount`,
  `-limitdescendantcount` and `-walletrejectlongchains` configuration
  arguments. (#15596)

- `getbalance` and `sendtoaddress`, plus the new RPCs `getbalances` and
  `createwallet`, now accept an "avoid_reuse" parameter that controls
  whether already used addresses should be included in the operation.
  Additionally, `sendtoaddress` will avoid partial spends when
  `avoid_reuse` is enabled even if this feature is not already enabled
  via the `-avoidpartialspends` command line flag because not doing so
  would risk using up the "wrong" UTXO for an address reuse case.
  (#13756)

- RPCs which have an `include_watchonly` argument or `includeWatching` option now default to `true` for watch-only
  wallets. Affected RPCs are: `getbalance`, `listreceivedbyaddress`, `listreceivedbylabel`, `listtransactions`,
  `listsinceblock`, `gettransaction`, `walletcreatefundedpsbt`, and `fundrawtransaction`. (#16383)

- `listunspent` now returns a "reused" bool for each output if the
  wallet flag "avoid_reuse" is enabled. (#13756)

- `getblockstats` now uses BlockUndo data instead of the transaction
  index, making it much faster, no longer dependent on the `-txindex`
  configuration option, and functional for all non-pruned blocks.
  (#14802)

- `utxoupdatepsbt` now accepts a `descriptors` parameter that will fill
  out input and output scripts and keys when known. P2SH-witness inputs
  will be filled in from the UTXO set when a descriptor is provided that
  shows they're spending segwit outputs.  See the RPC help text for full
  details. (#15427)

- `sendrawtransaction` and `testmempoolaccept` no longer accept a
  `allowhighfees` parameter to fail mempool acceptance if the
  transaction fee exceeds the value of the configuration option
  `-maxtxfee`.  Now there is a hardcoded default maximum feerate that
  can be changed when calling either RPC using a `maxfeerate` parameter.
  (#15620)

- `getmempoolancestors`, `getmempooldescendants`, `getmempoolentry`, and
  `getrawmempool` no longer return a `size` field unless the
  configuration option `-deprecatedrpc=size` is used.  Instead a new
  `vsize` field is returned with the transaction's virtual size
  (consistent with other RPCs such as `getrawtransaction`). (#15637)

- `getwalletinfo` now includes a `scanning` field that is either `false`
  (no scanning) or an object with information about the duration and
  progress of the wallet's scanning historical blocks for transactions
  affecting its balances. (#15730)

- `gettransaction` now accepts a third (boolean) argument `verbose`. If
  set to `true`, a new `decoded` field will be added to the response containing
  the decoded transaction. This field is equivalent to RPC `decoderawtransaction`,
  or RPC `getrawtransaction` when `verbose` is passed. (#16185, #16866, #16873)

- `createwallet` accepts a new `passphrase` parameter.  If set, this
  will create the new wallet encrypted with the given passphrase.  If
  unset (the default) or set to an empty string, no encryption will be
  used. (#16394)

- `getchaintxstats` RPC now returns the additional key of
  `window_final_block_height`. (#16695)

- `getmempoolentry` now provides a `weight` field containing the
  transaction weight as defined in BIP141. (#16647)

- The `getnetworkinfo` and `getpeerinfo` commands now contain a new field with decoded network service flags. (#16786)

- `getdescriptorinfo` now returns an additional `checksum` field
  containing the checksum for the unmodified descriptor provided by the
  user (that is, before the descriptor is normalized for the
  `descriptor` field). (#15986)

- `joinpsbts` now shuffles the order of the inputs and outputs of the resulting
  joined PSBT. Previously, inputs and outputs were added in the order PSBTs were
  provided. This made it easy to correlate inputs to outputs, representing a
  privacy leak. (#16512)

- `walletcreatefundedpsbt` now signals BIP125 Replace-by-Fee if the
  `-walletrbf` configuration option is set to true. (#15911)

GUI changes
-----------

- The GUI wallet now provides bech32 addresses by default.  The user may change the address type
  during invoice generation using a GUI toggle, or the default address
  type may be changed with the `-addresstype` configuration option.
  (#15711, #16497)

- In 0.18.0, a `./configure` flag was introduced to allow disabling BIP70 support in the GUI (support was enabled by default). In 0.19.0, this flag is now __disabled__ by default. If you want to compile Bitcoin Core with BIP70 support in the GUI, you can pass `--enable-bip70` to `./configure`. (#15584)

Deprecated or removed configuration options
-------------------------------------------

- `-mempoolreplacement` is removed, although default node behavior
  remains the same.  This option previously allowed the user to prevent
  the node from accepting or relaying BIP125 transaction replacements.
  This is different from the remaining configuration option
  `-walletrbf`. (#16171)

Deprecated or removed RPCs
--------------------------

- `bumpfee` no longer accepts a `totalFee` option unless the
  configuration parameter `deprecatedrpc=totalFee` is specified.  This
  parameter will be fully removed in a subsequent release. (#15996)

- `bumpfee` has a new `fee_rate` option as a replacement for the deprecated `totalFee`. (#16727)

- `generate` is now removed after being deprecated in Bitcoin Core 0.18.
  Use the `generatetoaddress` RPC instead. (#15492)

P2P changes
-----------

- BIP 61 reject messages were deprecated in v0.18. They are now disabled
  by default, but can be enabled by setting the `-enablebip61` command
  line option.  BIP 61 reject messages will be removed entirely in a
  future version of Bitcoin Core. (#14054)

- To eliminate well-known denial-of-service vectors in Bitcoin Core,
  especially for nodes with spinning disks, the default value for the
  `-peerbloomfilters` configuration option has been changed to false.
  This prevents Bitcoin Core from sending the BIP111 NODE_BLOOM service
  flag, accepting BIP37 bloom filters, or serving merkle blocks or
  transactions matching a bloom filter.  Users who still want to provide
  bloom filter support may either set the configuration option to true
  to re-enable both BIP111 and BIP37 support or ena