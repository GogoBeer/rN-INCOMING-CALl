Bitcoin Core version 0.13.1 is now available from:

  <https://bitcoin.org/bin/bitcoin-core-0.13.1/>

This is a new minor version release, including activation parameters for the
segwit softfork, various bugfixes and performance improvements, as well as
updated translations.

Please report bugs using the issue tracker at github:

  <https://github.com/bitcoin/bitcoin/issues>

To receive security and update notifications, please subscribe to:

  <https://bitcoincore.org/en/list/announcements/join/>

Compatibility
==============

Microsoft ended support for Windows XP on [April 8th, 2014](https://www.microsoft.com/en-us/WindowsForBusiness/end-of-xp-support),
an OS initially released in 2001. This means that not even critical security
updates will be released anymore. Without security updates, using a bitcoin
wallet on a XP machine is irresponsible at least.

In addition to that, with 0.12.x there have been varied reports of Bitcoin Core
randomly crashing on Windows XP. It is [not clear](https://github.com/bitcoin/bitcoin/issues/7681#issuecomment-217439891)
what the source of these crashes is, but it is likely that upstream
libraries such as Qt are no longer being tested on XP.

We do not have time nor resources to provide support for an OS that is
end-of-life. From 0.13.0 on, Windows XP is no longer supported. Users are
suggested to upgrade to a newer version of Windows, or install an alternative OS
that is supported.

No attempt is made to prevent installing or running the software on Windows XP,
you can still do so at your own risk, but do not expect it to work: do not
report issues about Windows XP to the issue tracker.

From 0.13.1 onwards OS X 10.7 is no longer supported. 0.13.0 was intended to work on 10.7+, 
but severe issues with the libc++ version on 10.7.x keep it from running reliably. 
0.13.1 now requires 10.8+, and will communicate that to 10.7 users, rather than crashing unexpectedly.

Notable changes
===============

Segregated witness soft fork
----------------------------

Segregated witness (segwit) is a soft fork that, if activated, will
allow transaction-producing software to separate (segregate) transaction
signatures (witnesses) from the part of the data in a transaction that is
covered by the txid. This provides several immediate benefits:

- **Elimination of unwanted transaction malleability:** Segregating the witness
  allows both existing and upgraded software to calculate the transaction
  identifier (txid) of transactions without referencing the witness, which can
  sometimes be changed by third-parties (such as miners) or by co-signers in a
  multisig spend. This solves all known cases of unwanted transaction
  malleability, which is a problem that makes programming Bitcoin wallet
  software more difficult and which seriously complicates the design of smart
  contracts for Bitcoin.

- **Capacity increase:** Segwit transactions contain new fields that are not
  part of the data currently used to calculate the size of a block, which
  allows a block containing segwit transactions to hold more data than allowed
  by the current maximum block size. Estimates based on the transactions
  currently found in blocks indicate that if all wallets switch to using
  segwit, the network will be able to support about 70% more transactions. The
  network will also be able to support more of the advanced-style payments
  (such as multisig) than it can support now because of the different weighting
  given to different parts of a transaction after segwit activates (see the
  following section for details).

- **Weighting data based on how it affects node performance:** Some parts of
  each Bitcoin block need to be stored by nodes in order to validate future
  blocks; other parts of a block can be immediately forgotten (pruned) or used
  only for helping other nodes sync their copy of the block chain.  One large
  part of the immediately prunable data are transaction signatures (witnesses),
  and segwit makes it possible to give a different "weight" to segregated
  witnesses to correspond with the lower demands they place on node resources.
  Specifically, each byte of a segregated witness is given a weight of 1, each
  other byte in a block is given a weight of 4, and the maximum allowed weight
  of a block is 4 million.  Weighting the data this way better aligns the most
  profitable strategy for creating blocks with the long-term costs of block
  validation.

- **Signature covers value:** A simple improvement in the way signatures are
  generated in segwit simplifies the design of secure signature generators
  (such as hardware wallets), reduces the amount of data the signature
  generator needs to download, and allows the signature generator to operate
  more quickly.  This is made possible by having the generator sign the amount
  of bitcoins they think they are spending, and by having full nodes refuse to
  accept those signatures unless the amount of bitcoins being spent is exactly
  the same as was signed.  For non-segwit transactions, wallets instead had to
  download the complete previous transactions being spent for every payment
  they made, which could be a slow operation on hardware wallets and in other
  situations where bandwidth or computation speed was constrained.

- **Linear scaling of sighash operations:** In 2015 a block was produced that
  required about 25 seconds to validate on modern hardware because of the way
  transaction signature hashes are performed.  Other similar blocks, or blocks
  that could take even longer to validate, can still be produced today.  The
  problem that caused this can't be fixed in a soft fork without unwanted
  side-effects, but transactions that opt-in to using segwit will now use a
  different signature method that doesn't suffer from this problem and doesn't
  have any unwanted side-effects.

- **Increased security for multisig:** Bitcoin addresses (both P2PKH addresses
  that start with a '1' and P2SH addresses that start with a '3') use a hash
  function known as RIPEMD-160.  For P2PKH addresses, this provides about 160
  bits of security---which is beyond what cryptographers believe can be broken
  today.  But because P2SH is more flexible, only about 80 bits of security is
  provided per address. Although 80 bits is very strong security, it is within
  the realm of possibility that it can be broken by a powerful adversary.
  Segwit allows advanced transactions to use the SHA256 hash function instead,
  which provides about 128 bits of security  (that is 281 trillion times as
  much security as 80 bits and is equivalent to the maximum bits of security
  believed to be provided by Bitcoin's choice of parameters for its Elliptic
  Curve Digital Security Algorithm [ECDSA].)

- **More efficient almost-full-node security** Satoshi Nakamoto's original
  Bitcoin paper describes a method for allowing newly-started full nodes to
  skip downloading and validating some data from historic blocks that are
  protected by large amounts of proof of work.  Unfortunately, Nakamoto's
  method can't guarantee that a newly-started node using this method will
  produce an accurate copy of Bitcoin's current ledger (called the UTXO set),
  making the node vulnerable to falling out of consensus with other nodes.
  Although the problems with Nakamoto's method can't be fixed in a soft fork,
  Segwit accomplishes something similar to his original proposal: it makes it
  possible for a node to optionally skip downloading some blockchain data
  (specifically, the segregated witnesses) while still ensuring that the node
  can build an accurate copy of the UTXO set for the block chain with the most
  proof of work.  Segwit enables this capability at the consensus layer, but
  note that Bitcoin Core does not provide an option to use this capability as
  of this 0.13.1 release.

- **Script versioning:** Segwit makes it easy for future soft forks to allow
  Bitcoin users to individually opt-in to almost any change in the Bitcoin
  Script language when those users receive new transactions.  Features
  currently being researched by Bitcoin Core contributors that may use this
  capability include support for Schnorr signatures, which can improve the
  privacy and 