// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <netaddress.h>

#include <crypto/common.h>
#include <crypto/sha3.h>
#include <hash.h>
#include <prevector.h>
#include <tinyformat.h>
#include <util/asmap.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ios>
#include <iterator>
#include <tuple>

constexpr size_t CNetAddr::V1_SERIALIZATION_SIZE;
constexpr size_t CNetAddr::MAX_ADDRV2_SIZE;

CNetAddr::BIP155Network CNetAddr::GetBIP155Network() const
{
    switch (m_net) {
    case NET_IPV4:
        return BIP155Network::IPV4;
    case NET_IPV6:
        return BIP155Network::IPV6;
    case NET_ONION:
        return BIP155Network::TORV3;
    case NET_I2P:
        return BIP155Network::I2P;
    case NET_CJDNS:
        return BIP155Network::CJDNS;
    case NET_INTERNAL:   // should have been handled before calling this function
    case NET_UNROUTABLE: // m_net is never and should not be set to NET_UNROUTABLE
    case NET_MAX:        // m_net is never and should not be set to NET_MAX
        assert(false);
    } // no default case, so the compiler can warn about missing cases

    assert(false);
}

bool CNetAddr::SetNetFromBIP155Network(uint8_t possible_bip155_net, size_t address_size)
{
    switch (possible_bip155_net) {
    case BIP155Network::IPV4:
        if (address_size == ADDR_IPV4_SIZE) {
            m_net = NET_IPV4;
            return true;
        }
        throw std::ios_base::failure(
            strprintf("BIP155 IPv4 address with length %u (should be %u)", address_size,
                      ADDR_IPV4_SIZE));
    case BIP155Network::IPV6:
        if (address_size == ADDR_IPV6_SIZE) {
            m_net = NET_IPV6;
            return true;
        }
        throw std::ios_base::failure(
            strprintf("BIP155 IPv6 address with length %u (should be %u)", address_size,
                      ADDR_IPV6_SIZE));
    case BIP155Network::TORV3:
        if (address_size == ADDR_TORV3_SIZE) {
            m_net = NET_ONION;
            return true;
        }
        throw std::ios_base::failure(
            strprintf("BIP155 TORv3 address with length %u (should be %u)", address_size,
                      ADDR_TORV3_SIZE));
    case BIP155Network::I2P:
        if (address_size == ADDR_I2P_SIZE) {
            m_net = NET_I2P;
            return true;
        }
        throw std::ios_base::failure(
            strprintf("BIP155 I2P address with length %u (should be %u)", address_size,
                      ADDR_I2P_SIZE));
    case