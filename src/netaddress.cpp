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
    case BIP155Network::CJDNS:
        if (address_size == ADDR_CJDNS_SIZE) {
            m_net = NET_CJDNS;
            return true;
        }
        throw std::ios_base::failure(
            strprintf("BIP155 CJDNS address with length %u (should be %u)", address_size,
                      ADDR_CJDNS_SIZE));
    }

    // Don't throw on addresses with unknown network ids (maybe from the future).
    // Instead silently drop them and have the unserialization code consume
    // subsequent ones which may be known to us.
    return false;
}

/**
 * Construct an unspecified IPv6 network address (::/128).
 *
 * @note This address is considered invalid by CNetAddr::IsValid()
 */
CNetAddr::CNetAddr() {}

void CNetAddr::SetIP(const CNetAddr& ipIn)
{
    // Size check.
    switch (ipIn.m_net) {
    case NET_IPV4:
        assert(ipIn.m_addr.size() == ADDR_IPV4_SIZE);
        break;
    case NET_IPV6:
        assert(ipIn.m_addr.size() == ADDR_IPV6_SIZE);
        break;
    case NET_ONION:
        assert(ipIn.m_addr.size() == ADDR_TORV3_SIZE);
        break;
    case NET_I2P:
        assert(ipIn.m_addr.size() == ADDR_I2P_SIZE);
        break;
    case NET_CJDNS:
        assert(ipIn.m_addr.size() == ADDR_CJDNS_SIZE);
        break;
    case NET_INTERNAL:
        assert(ipIn.m_addr.size() == ADDR_INTERNAL_SIZE);
        break;
    case NET_UNROUTABLE:
    case NET_MAX:
        assert(false);
    } // no default case, so the compiler can warn about missing cases

    m_net = ipIn.m_net;
    m_addr = ipIn.m_addr;
}

void CNetAddr::SetLegacyIPv6(Span<const uint8_t> ipv6)
{
    assert(ipv6.size() == ADDR_IPV6_SIZE);

    size_t skip{0};

    if (HasPrefix(ipv6, IPV4_IN_IPV6_PREFIX)) {
        // IPv4-in-IPv6
        m_net = NET_IPV4;
        skip = sizeof(IPV4_IN_IPV6_PREFIX);
    } else if (HasPrefix(ipv6, TORV2_IN_IPV6_PREFIX)) {
        // TORv2-in-IPv6 (unsupported). Unserialize as !IsValid(), thus ignoring them.
        // Mimic a default-constructed CNetAddr object which is !IsValid() and thus
        // will not be gossiped, but continue reading next addresses from the stream.
        m_net = NET_IPV6;
        m_addr.assign(ADDR_IPV6_SIZE, 0x0);
        return;
    } else if (HasPrefix(ipv6, INTERNAL_IN_IPV6_PREFIX)) {
        // Internal-in-IPv6
        m_net = NET_INTERNAL;
        skip = sizeof(INTERNAL_IN_IPV6_PREFIX);
    } else {
        // IPv6
        m_net = NET_IPV6;
    }

    m_addr.assign(ipv6.begin() + skip, ipv6.end());
}

/**
 * Create an "internal" address that represents a name or FQDN. AddrMan uses
 * these fake addresses to keep track of which DNS seeds were used.
 * @returns Whether or not the operation was successful.
 * @see NET_INTERNAL, INTERNAL_IN_IPV6_PREFIX, CNetAddr::IsInternal(), CNetAddr::IsRFC4193()
 */
bool CNetAddr::SetInternal(const std::string &name)
{
    if (name.empty()) {
        return false;
    }
    m_net = NET_INTERNAL;
    unsigned char hash[32] = {};
    CSHA256().Write((const unsigned char*)name.data(), name.size()).Finalize(hash);
    m_addr.assign(hash, hash + ADDR_INTERNAL_SIZE);
    return true;
}

namespace torv3 {
// https://gitweb.torproject.org/torspec.git/tree/rend-spec-v3.txt#n2135
static constexpr size_t CHECKSUM_LEN = 2;
static const unsigned char VERSION[] = {3};
static constexpr size_t TOTAL_LEN = ADDR_TORV3_SIZE + CHECKSUM_LEN + sizeof(VERSION);

static void Checksum(Span<const uint8_t> addr_pubkey, uint8_t (&checksum)[CHECKSUM_LEN])
{
    // TORv3 CHECKSUM = H(".onion checksum" | PUBKEY | VERSION)[:2]
    static const unsigned char prefix[] = ".onion checksum";
    static constexpr size_t prefix_len = 15;

    SHA3_256 hasher;

    hasher.Write(Span{prefix}.first(prefix_len));
    hasher.Write(addr_pubkey);
    hasher.Write(VERSION);

    uint8_t checksum_full[SHA3_256::OUTPUT_SIZE];

    hasher.Finalize(checksum_full);

    memcpy(checksum, checksum_full, sizeof(checksum));
}

}; // namespace torv3

bool CNetAddr::SetSpecial(const std::string& addr)
{
    if (!ValidAsCString(addr)) {
        return false;
    }

    if (SetTor(addr)) {
        return true;
    }

    if (SetI2P(addr)) {
        return true;
    }

    return false;
}

bool CNetAddr::SetTor(const std::string& addr)
{
    static const char* suffix{".onion"};
    static constexpr size_t suffix_len{6};

    if (addr.size() <= suffix_len || addr.substr(addr.size() - suffix_len) != suffix) {
        return false;
    }

    bool invalid;
    const auto& input = DecodeBase32(addr.substr(0, addr.size() - suffix_len).c_str(), &invalid);

    if (invalid) {
        return false;
    }

    if (input.size() == torv3::TOTAL_LEN) {
        Span<const uint8_t> input_pubkey{input.data(), ADDR_TORV3_SIZE};
        Span<const uint8_t> input_checksum{input.data() + ADDR_TORV3_SIZE, torv3::CHECKSUM_LEN};
        Span<const uint8_t> input_version{input.data() + ADDR_TORV3_SIZE + torv3::CHECKSUM_LEN, sizeof(torv3::VERSION)};

        if (input_version != torv3::VERSION) {
            return false;
        }

        uint8_t calculated_checksum[torv3::CHECKSUM_LEN];
        torv3::Checksum(input_pubkey, calculated_checksum);

        if (input_checksum != calculated_checksum) {
            return false;
        }

        m_net = NET_ONION;
        m_addr.assign(input_pubkey.begin(), input_pubkey.end());
        return true;
    }

    return false;
}

bool CNetAddr::SetI2P(const std::string& addr)
{
    // I2P addresses that we support consist of 52 base32 characters + ".b32.i2p".
    static constexpr size_t b32_len{52};
    static const char* suffix{".b32.i2p"};
    static constexpr size_t suffix_len{8};

    if (addr.size() != b32_len + suffix_len || ToLower(addr.substr(b32_len)) != suffix) {
        return false;
    }

    // Remove the ".b32.i2p" suffix and pad to a multiple of 8 chars, so DecodeBase32()
    // can decode it.
    const std::string b32_padded = addr.substr(0, b32_len) + "====";

    bool invalid;
    const auto& address_bytes = DecodeBase32(b32_padded.c_str(), &invalid);

    if (invalid || address_bytes.size() != ADDR_I2P_SIZE) {
        return false;
    }

    m_net = NET_I2P;
    m_addr.assign(address_bytes.begin(), address_bytes.end());

    return true;
}

CNetAddr::CNetAddr(const struct in_addr& ipv4Addr)
{
    m_net = NET_IPV4;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&ipv4Addr);
    m_addr.assign(ptr, ptr + ADDR_IPV4_SIZE);
}

CNetAddr::CNetAddr(const struct in6_addr& ipv6Addr, const uint32_t scope)
{
    SetLegacyIPv6({reinterpret_cast<const uint8_t*>(&ipv6Addr), sizeof(ipv6Addr)});
    m_scope_id = scope;
}

bool CNetAddr::IsBindAny() const
{
    if (!IsIPv4() && !IsIPv6()) {
        return false;
    }
    return std::all_of(m_addr.begin(), m_addr.end(), [](uint8_t b) { return b == 0; });
}

bool CNetAddr::IsIPv4() const { return m_net == NET_IPV4; }

bool CNetAddr::IsIPv6() const { return m_net == NET_IPV6; }

bool CNetAddr::IsRFC1918() const
{
    return IsIPv4() && (
        m_addr[0] == 10 ||
        (m_addr[0] == 192 && m_addr[1] == 168) ||
        (m_addr[0] == 172 && m_addr[1] >= 16 && m_addr[1] <= 31));
}

bool CNetAddr::IsRFC2544() const
{
    return IsIPv4() && m_addr[0] == 198 && (m_addr[1] == 18 || m_addr[1] == 19);
}

bool CNetAddr::IsRFC3927() const
{
    return IsIPv4() && HasPrefix(m_addr, std::array<uint8_t, 2>{169, 254});
}

bool CNetAddr::IsRFC6598() const
{
    return IsIPv4() && m_addr[0] == 100 && m_addr[1] >= 64 && m_addr[1] <= 127;
}

bool CNetAddr::IsRFC5737() const
{
    return IsIPv4() && (HasPrefix(m_addr, std::array<uint8_t, 3>{192, 0, 2}) ||
                        HasPrefix(m_addr, std::array<uint8_t, 3>{198, 51, 100}) ||
                        HasPrefix(m_addr, std::array<uint8_t, 3>{203, 0, 113}));
}

bool CNetAddr::IsRFC3849() const
{
    return IsIPv6() && HasPrefix(m_addr, std::arr