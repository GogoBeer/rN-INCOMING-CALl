// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <qt/receivecoinsdialog.h>
#include <qt/forms/ui_receivecoinsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receiverequestdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/walletmodel.h>

#include <QAction>
#include <QCursor>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>

ReceiveCoinsDialog::ReceiveCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::ReceiveCoinsDialog),
    model(nullptr),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons()) {
        ui->clearButton->setIcon(QIcon());
        ui->receiveButton->setIcon(QIcon());
        ui->showRequestButton->setIcon(QIcon());
        ui->removeRequestButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->receiveButton->setIcon(_platformStyle->SingleColorIcon(":/icons/receiving_addresses"));
        ui->showRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/eye"));
        ui->removeRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
    }

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(tr("Copy &URI"), this, &ReceiveCoinsDialog::copyURI);
    contextMenu->addAction(tr("&Copy address"), this, &ReceiveCoinsDialog::copyAddress);
    copyLabelAction = contextMenu->addAction(tr("Copy &label"), this, &ReceiveCoinsDialog::copyLabel);
    copyMessageAction = contextMenu->addAction(tr("Copy &message"), this, &ReceiveCoinsDialog::copyMessage);
    copyAmountAction = contextMenu->addAction(tr("Copy &amount"), this, &ReceiveCoinsDialog::copyAmount);
    connect(ui->recentRequestsView, &QWidget::customContextMenuRequested, this, &ReceiveCoinsDialog::showMenu);

    connect(ui->clearButton, &QPushButton::clicked, this, &ReceiveCoinsDialog::clear);

    QTableView* tableView = ui->recentRequestsView;
    tableView->verticalHeader()->hide();
    tableView->setAlternatingRowColors(true);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);

    QSettings settings;
    if (!tableView->horizontalHeader()->restoreState(settings.value("RecentRequestsViewHeaderState").toByteArray())) {
        tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);
        tableView->horizontalHeader()->setMinimumSectionSize(MINIMUM_COLUMN_WIDTH);
        tableView->horizontalHeader()->setStretchLastSection(true);
    }
}

void ReceiveCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        _model->getRecentRequestsTableModel()->sort(RecentRequestsTableModel::Date, Qt::DescendingOrder);
        connect(_model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &ReceiveCoinsDialog::updateDisplayUnit);
        updateDisplayUnit();

        QTableView* tableView = ui->recentRequestsView;
        tableView->setModel(_model->getRecentRequestsTableModel());
        tableView->sortByColumn(RecentRequestsTableModel::Date, Qt::DescendingOrder);

        connect(tableView->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            &ReceiveCoinsDialog::recentRequestsView_selectionChanged);

        // Populate address type dropdown and select default
        auto add_address_type = [&](OutputType type, const QString& text, const QString& tooltip) {
            const auto index = ui->addressType->count();
            ui->addressType->addItem(text, (int) type);
            ui->addressType->setItemData(index, tooltip, Qt::ToolTipRole);
            if (model->wallet().getDefaultAddressType() == type) ui->addressType->setCurrentIndex(index);
        };
        add_address_type(OutputType::LEGACY, "Base58 (Legacy)", "Not recommended due to higher fees and less protection against typos.");
        add_address_type(OutputType::P2SH_SEGWIT, "Base58 (P2SH-SegWit)", "Generates an address compatible with older wallets.");
        add_address_type(OutputType::BECH32, "Bech32 (SegWit)", "Generates a native segwit address (BIP-173). Some old wallets don't support it.");
        if (model->wallet().taprootEnabled()) {
            add_address_type(OutputType::BECH32M, "Bech32m (Taproot)", "Bech32m (BIP-350) is an upgrade to Bech32, wallet support is still limited.");
        }

        // Set the button to be enabled or disabled based on whether the wallet can give out new addresses.
        ui->receiveButton->setEnabled(model->wallet().canGetAddresses());

        // Enable/disable the receive button if the wallet is now able/unable to give out new addresses.
        connect(model, &WalletModel::canGetAddressesChanged, [this] {
            ui->receiveButton->setEnabled(model->wallet().canGetAddresses());
        });
    }
}

ReceiveCoinsDialog::~ReceiveCoinsDialog()
{
    QSettings settings;
    settings.setValue("RecentRequestsViewHeaderState", ui->recentRequestsView->horizontalHeader()->saveState());
    delete ui;
}

void ReceiveCoinsDialog