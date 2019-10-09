// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <banman.h>

#include <netaddress.h>
#include <node/ui_interface.h>
#include <util/system.h>
#include <util/time.h>
#include <util/translation.h>


BanMan::BanMan(fs::path ban_file, CClientUIInterface* client_interface, int64_t default_ban_time)
    : m_client_interface(client_interface), m_ban_db(std::move(ban_file)), m_default_ban_time(default_ban_time)
{
    if (m_client_interface) m_client_interface->InitMessage(_("Loading banlist…").translated);

    int64_t n_start = GetTimeMillis();
    if (m_ban_db.Read(m_banned)) {
        SweepBanned(); // sweep out unused entries

        LogPrint(BCLog::NET, "Loaded %d banned node addresses/subnets  %dms\n", m_banned.size(),
                 GetTimeMillis() - n_start);
    } else {
        LogPrintf("Recreating the banlist database\n");
        m_banned = {};
        m_is_dirty = true;
    }

    DumpBanlist();
}

BanMan::~BanMan()
{
    DumpBanlist();
}

void BanMan::DumpBanlist()
{
    SweepBanned(); // clean unused entries (if bantime has expired)

    if (!BannedSetIsDirty()) return;

    int64_t n_start = GetTimeMillis();

    banmap_t banmap;
    GetBanned(banmap);
    if (m_ban_db.Write(banmap)) {
        SetBannedSetDirty(false);
    }

    LogPrint(BCLog::NET, "Flushed %d banned node addresses/subnets to disk  %dms\n", banmap.size(),
             GetTimeMillis() - n_start);
}

void BanMan::ClearBanned()
{
    {
        LOCK(m_cs_banned);
        m_banned.clear();
        m_is_dirty = true;
    }
    DumpBanlist(); //store banlist to disk
    if (m_client_interface) m_client_interface->BannedListChanged();
}

bool BanMan::IsDiscouraged(const CNetAddr& net_addr)
{
    LOCK(m_cs_banned);
    return m_discouraged.contains(net_addr.GetAddrBytes());
}

bool BanMan::IsBanned(const CNetAddr& net_addr)
{
    auto current_time = GetTime();
    LOCK(m_cs_banned);
    for (const auto& it : m_banned) {
        CSubNet sub_net = it.first;
        CBanEntry ban_entry = it.second;

        if (current_time < ban_entry.nBanUntil && sub_net.Match(net_addr)) {
            return true;
        }
    }
    return false;
}

bool BanMan::IsBanned(const CSubNet& sub_net)
{
    auto current_time = GetTime();
    LOCK(m_cs_banned);
    banmap_t::iterator i = m_banned.find(sub_net);
    if (i != m_banned.end()) {
        CBanEntry ban_entry = (*i).second;
        if (current_time < ban_entry.nBanUntil) {
            return true;
        }
    }
    return false;
}

void BanMan::Ban(const CNetAddr& net_addr, int64_t ban_time_offset, bool since_unix_epoch)
{
    CSubNet sub_net(net_addr);
    Ban(sub_net, ban_time_offset, since_unix_epoch);
}

void BanMan::Discourage(const CNetAddr& net_addr)
{
    LOCK(m_cs_banned);
    m_discouraged.insert(net_addr.GetAddrBytes());
}

void B