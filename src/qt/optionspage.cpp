// Copyright (c) 2011-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "optionspage.h"
#include "ui_optionspage.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "bitcoingui.h"
#include "optionsmodel.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "walletmodel.h"

#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
#include <QDataWidgetMapper>
#include <QDoubleValidator>
#include <QFile>
#include <QTextStream>

using namespace std;

OptionsPage::OptionsPage(QWidget* parent) : QDialog(parent),
                                                          ui(new Ui::OptionsPage),
                                                          model(0),
                                                          // m_SizeGrip(this),
                                                          mapper(0)
{
    ui->setupUi(this);

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);

    ui->toggleTheme->setState(settings.value("theme")!="light");
    connect(ui->toggleTheme, SIGNAL(stateChanged(ToggleButton*)), this, SLOT(changeTheme(ToggleButton*)));

    connect(ui->lineEditNewPass, SIGNAL(textChanged(const QString &)), this, SLOT(validateNewPass()));
    connect(ui->lineEditNewPassRepeat, SIGNAL(textChanged(const QString &)), this, SLOT(validateNewPassRepeat()));
    connect(ui->lineEditOldPass, SIGNAL(textChanged(const QString &)), this, SLOT(onOldPassChanged()));

    QDoubleValidator *dblVal = new QDoubleValidator(0, Params().MAX_MONEY, 6, ui->lineEditWithhold);
    dblVal->setNotation(QDoubleValidator::StandardNotation);
    dblVal->setLocale(QLocale::C);
    ui->lineEditWithhold->setValidator(dblVal);
    ui->lineEditWithhold->setPlaceholderText("DAPS Amount");
    if (nReserveBalance > 0)
        ui->lineEditWithhold->setText(BitcoinUnits::format(0, nReserveBalance).toUtf8());

    bool stkStatus = pwalletMain->ReadStakingStatus();
    ui->toggleStaking->setState(nLastCoinStakeSearchInterval | stkStatus);
    connect(ui->toggleStaking, SIGNAL(stateChanged(ToggleButton*)), this, SLOT(on_EnableStaking(ToggleButton*)));
}

void OptionsPage::setModel(WalletModel* model)
{
    this->model = model;
    this->options = model->getOptionsModel();

    if (model && model->getOptionsModel()) {
        model->getRecentRequestsTableModel()->sort(RecentRequestsTableModel::Date, Qt::DescendingOrder);
    }

    mapper->setModel(options);
    setMapper();
    mapper->toFirst();
}

static inline int64_t roundint64(double d)
{
    return (int64_t)(d > 0 ? d + 0.5 : d - 0.5);
}

CAmount OptionsPage::getValidatedAmount() {
    double dAmount = ui->lineEditWithhold->text().toDouble();
    if (dAmount < 0.0 || dAmount > Params().MAX_MONEY)
        throw runtime_error("Invalid amount, amount should be < 2.1B DAPS");
    CAmount nAmount = roundint64(dAmount * COIN);
    return nAmount;
}

OptionsPage::~OptionsPage()
{
    delete ui;
}

void OptionsPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

void OptionsPage::on_pushButtonSave_clicked() {
    if (ui->lineEditWithhold->text().trimmed().isEmpty()) {
        QMessageBox(QMessageBox::Information, tr("Information"), tr("DAPS reserve amount should be filled"), QMessageBox::Ok).exec();
        return;
    }
    nReserveBalance = getValidatedAmount();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    walletdb.WriteReserveAmount(nReserveBalance / COIN);

    emit model->stakingStatusChanged(nLastCoinStakeSearchInterval);
    QMessageBox(QMessageBox::Information, tr("Information"), tr("Reserve balance " + BitcoinUnits::format(0, nReserveBalance).toUtf8() + " is successfully set!"), QMessageBox::Ok).exec();
}


void OptionsPage::keyPressEvent(QKeyEvent* event)
{

    this->QDialog::keyPressEvent(event);
}

void OptionsPage::setMapper()
{
}

void OptionsPage::on_pushButtonPassword_clicked()
{
    //disable password submit button
    SecureString oldPass = SecureString();
    oldPass.reserve(MAX_PASSPHRASE_SIZE);
    oldPass.assign( ui->lineEditOldPass->text().toStdString().c_str() );
    SecureString newPass = SecureString();
    newPass.reserve(MAX_PASSPHRASE_SIZE);
    newPass.assign( ui->lineEditNewPass->text().toStdString().c_str() );

    SecureString newPass2 = SecureString();
    newPass2.reserve(MAX_PASSPHRASE_SIZE);
    newPass2.assign(ui->lineEditNewPassRepeat->text().toStdString().c_str() );

    bool success = false;


    if (newPass == newPass2) {
    	if (model->changePassphrase(oldPass, newPass)) {
    		QMessageBox::information(this, tr("Wallet encrypted"),
    				tr("Wallet passphrase was successfully changed."));
    		success = true;
    	} else {
    		QMessageBox::critical(this, tr("Wallet encryption failed"),
    				tr("The passphrase entered for the wallet decryption was incorrect."));
    	}
    }

    if (success)
        ui->pushButtonPassword->setStyleSheet("border: 2px solid green");
    else ui->pushButtonPassword->setStyleSheet("border: 2px solid red");
    ui->pushButtonPassword->repaint();
}

void OptionsPage::on_pushButtonBackup_clicked(){
    if (model->backupWallet(QString("BackupWallet.dat"))) {
        ui->pushButtonBackup->setStyleSheet("border: 2px solid green");
        QMessageBox(QMessageBox::Information, tr("Information"), tr("Wallet has been successfully backed up to BackupWallet.dat in the current directory."), QMessageBox::Ok).exec();
    } else { ui->pushButtonBackup->setStyleSheet("border: 2px solid red");
        QMessageBox::critical(this, tr("Error"),tr("Wallet backup failed. Please try again."));
}
    ui->pushButtonBackup->repaint();
}

void OptionsPage::validateNewPass()
{
    if ( (ui->lineEditNewPass->text().contains(" ")) || (!ui->lineEditNewPass->text().length()) )
        ui->lineEditNewPass->setStyleSheet("border-color: red");
    else ui->lineEditNewPass->setStyleSheet(GUIUtil::loadStyleSheet());
    matchNewPasswords();
    ui->lineEditNewPass->repaint();
}

void OptionsPage::validateNewPassRepeat()
{
    matchNewPasswords();
}

void OptionsPage::onOldPassChanged()
{
    QString stylesheet = GUIUtil::loadStyleSheet();
    ui->lineEditOldPass->setStyleSheet(stylesheet);
    ui->lineEditOldPass->repaint();
    ui->pushButtonPassword->setStyleSheet(stylesheet);
    ui->pushButtonPassword->repaint();
    if (!ui->lineEditNewPass->text().length())
        ui->lineEditNewPass->setStyleSheet("border-color: red");
    ui->lineEditNewPass->repaint();
}

bool OptionsPage::matchNewPasswords()
{
    if (ui->lineEditNewPass->text()==ui->lineEditNewPassRepeat->text())
    {
        ui->lineEditNewPassRepeat->setStyleSheet(GUIUtil::loadStyleSheet());
        ui->lineEditNewPassRepeat->repaint();
        return true;
    } else
    {
        ui->lineEditNewPassRepeat->setStyleSheet("border-color: red");
        ui->lineEditNewPassRepeat->repaint();
        return false;
    }
}

void OptionsPage::on_EnableStaking(ToggleButton* widget)
{
    if (chainActive.Height() < Params().LAST_POW_BLOCK()) {
    	if (widget->getState()) {
			QString msg("PoW blocks are still being mined!");
			QStringList l;
			l.push_back(msg);
			GUIUtil::prompt(QString("<br><br>")+l.join(QString("<br><br>"))+QString("<br><br>"));
    	}
    	widget->setState(false);
    	pwalletMain->WriteStakingStatus(false);
    	pwalletMain->walletStakingInProgress = false;
        return;
    }
	if (widget->getState()){
        QStringList errors = model->getStakingStatusError();
        if (!errors.length()) {
            pwalletMain->WriteStakingStatus(true);
            emit model->stakingStatusChanged(true);
            model->generateCoins(true, 1);
        } else {
            GUIUtil::prompt(QString("<br><br>")+errors.join(QString("<br><br>"))+QString("<br><br>"));
            widget->setState(false);
            nLastCoinStakeSearchInterval = 0;
            emit model->stakingStatusChanged(false);
            pwalletMain->WriteStakingStatus(false);
        }
    } else {
        nLastCoinStakeSearchInterval = 0;
        model->generateCoins(false, 0);
        emit model->stakingStatusChanged(false);
        pwalletMain->walletStakingInProgress = false;
        pwalletMain->WriteStakingStatus(false);
    }
}

void OptionsPage::changeTheme(ToggleButton* widget)
{
    if (widget->getState())
        settings.setValue("theme", "dark");
    else settings.setValue("theme", "light");
    GUIUtil::refreshStyleSheet();
}