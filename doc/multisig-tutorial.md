# 1. Multisig Tutorial

Currently, it is possible to create a multisig wallet using Bitcoin Core only.

Although there is already a brief explanation about the multisig in the [Descriptors documentation](https://github.com/bitcoin/bitcoin/blob/master/doc/descriptors.md#multisig), this tutorial proposes to use the signet (instead of regtest), bringing the reader closer to a real environment and explaining some functions in more detail.

This tutorial uses [jq](https://github.com/stedolan/jq) JSON processor to process the results from RPC and stores the relevant values in bash variables. This makes the tutorial reproducible and easier to follow step by step.

Before starting this tutorial, start the bitcoin node on the signet network.

```bash
./src/bitcoind -signet -daemon
```

This tutorial also uses the default WPKH derivation path to get the xpubs and does not conform to [BIP 45](https://github.com/bitcoin/bips/blob/master/bip-0045.mediawiki) or [BIP 87](https://github.com/bitcoin/bips/blob/master/bip-0087.mediawiki).

At the time of writing, there is no way to extract a specific path from wallets in Bitcoin Core. For this, an external signer/xpub can be used.

[PR #22341](https://github.com/bitcoin/bitcoin/pull/22341), which is still under development, introduces a new wallet RPC `getxpub`. It takes a BIP32 path as an argument and returns the xpub, along with the master key fingerprint.

## 1.1 Basic Multisig Workflow

### 1.1 Create the Descriptor Wallets

For a 2-of-3 multisig, create 3 descriptor wallets. It is important that they are of the descriptor type in order to retrieve the wallet descriptors. These wallets contain HD seed and private keys, which will be used to sign the PSBTs and derive the xpub.

These three wallets should not be used directly for privacy reasons (public key reuse). They should only be used to sign transactions for the (watch-only) multisig wallet.

```bash
for ((n=1;n<=3;n++))
do
 ./src/bitcoin-cli -signet -named createwallet wallet_name="participant_${n}" descriptors=true
done
```

Extract the xpub of each wallet. To do this, the `listdescriptors` RPC is used. By default, Bitcoin Core single-sig wallets are created using path `m/44'/1'/0'` for PKH, `m/84'/1'/0'` for WPKH, `m/49'/1'/0'` for P2WPKH-nested-in-P2SH and `m/86'/1'/0'` for P2TR based accounts. Each of them uses the chain 0 for external addresses and chain 1 for internal ones, as shown in the example below.

```
wpkh([1004658e/84'/1'/0']tpubDCBEcmVKbfC9KfdydyLbJ2gfNL88grZu1XcWSW9ytTM6fitvaRmVyr8Ddf7SjZ2ZfMx9RicjYAXhuh3fmLiVLPodPEqnQQURUfrBKiiVZc8/0/*)#g8l47ngv

wpkh([1004658e/84'/1'/0']tpubDCBEcmVKbfC9KfdydyLbJ2gfNL88grZu1XcWSW9ytTM6fitvaRmVyr8Ddf7SjZ2ZfMx9RicjYAXhuh3fmLiVLPodPEqnQQURUfrBKiiVZc8/1/*)#en65rxc5
```

The suffix (after #) is the checksum. Descriptors can optionally be suffixed with a checksum to protect against typos or copy-paste errors.
All RPCs in Bitcoin Core will include the checksum in their output.

```bash
declare -A xpubs

for ((n=1;n<=3;n++))
do
 xpubs["internal_xpub_${n}"]=$(./src/bitcoin-cli -signet -rpcwallet="participant_${n}" listdescriptors | jq '.descriptors | [.[] | select(.desc | startswith("wpkh") and contains("/1/*"))][0] | .desc' | grep -Po '(?<=\().*(?=\))')

 xpubs["external_xpub_${n}"]=$(./src/bitcoin-cli -signet -rpcwallet="participant_${n}" listdescriptors | jq '.descriptors | [.[] | select(.desc | startswith("wpkh") and contains("/0/*") )][0] | .desc' | grep -Po '(?<=\().*(?=\))')
done
```

`jq` is used to extract the xpub from the `wpkh` descriptor.

The following command can be used to verify if the xpub was generated correctly.

```bash
for x in "${!xpubs[@]}"; do printf "[%s]=%s\n" "$x" "${xpubs[$x]}" ; done
```

As previously mentioned, this step extracts the `m/84'/1'/0'` account instead of the path defined in [BIP 45](https://github.com/bitcoin/bips/blob/master/bip-0045.mediawiki) or [BIP 87](https://github.com/bitcoin/bips/blob/master/bip-0087.mediawiki), since there is no way to extract a specific path in Bitcoin Core at the time of writing.

### 1.2 Define the Multisig Descriptors

Define the external and internal multisig descriptors, add the checksum and then, join both in a JSON array.

```bash
external_desc="wsh(sortedmulti(2,${xpubs["external_xpub_1"]},${xpubs["external_xpub_2"]},${xpubs["external_xpub_3"]}))"
internal_desc="wsh(sortedmulti(2,${xpubs["internal_xpub_1"]},${xpubs["internal_xpub_2"]},${xpubs["internal_xpub_3"]}))"

external_desc_sum=$(./src/bitcoin-cli -signet getdescriptorinfo $external_desc | jq '.descriptor')
internal_desc_sum=$(./src/bitcoin-cli -signet getdescriptorinfo $internal_desc | jq '.descriptor')

multisig_ext_desc="{\"desc\": $external_desc_sum, \"active\": true, \"internal\": false, \"timestamp\": \"now\"}"
multisig_int_desc="{\"desc\": $internal_desc_sum, \"active\": true, \"internal\": true, \"timestamp\": \"now\"}"

multisig_desc="[$multisig_ext_desc, $multisig_int_desc]"
```

`external_desc` and `internal_desc` specify the output type (`wsh`, in this case) and the xpubs involved. They al