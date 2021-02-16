// Copyright (c) 2017-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/addressbooktests.h>
#include <qt/test/util.h>
#include <test/util/setup_common.h>

#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <qt/clientmodel.h>
#include <qt/editaddressdialog.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/walletmodel.h>

#include <key.h>
#include <key_io.h>
#include <wallet/wallet.h>
#include <walletinitinterface.h>

#include <QApplication>
#include <QTimer>
#include <QMessageBox>

namespace
{

/**
 * Fill the edit address d