// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/bitcoin.h>

#include <chainparams.h>
#include <init.h>
#include <interfaces/handler.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <node/ui_interface.h>
#include <noui.h>
#include <qt/bitcoingui.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/initexecutor.h>
#include <qt/intro.h>
#include <qt/networkstyle.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/splashscreen.h>
#include <qt/utilitydialog.h>
#include <qt/winshutdownmonitor.h>
#include <uint256.h>
#include <util/string.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <validation.h>

#ifdef ENABLE_WALLET
#include <qt/paymentserver.h>
#include <qt/walletcontroller.h>
#include <qt/walletmodel.h>
#endif // ENABLE_WALLET

#include <boost/signals2/connection.hpp>
#include <memory>

#include <QApplication>
#include <QDebug>
#include <QLatin1String>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QWindow>

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
#if defined(QT_QPA_PLATFORM_WINDOWS)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin);
#elif defined(QT_QPA_PLATFORM_COCOA)
Q_IMPORT_PLUGIN(QMacStylePlugin);
#endif
#endif

// Declare meta types used for QMetaObject::invokeMethod
Q_DECLARE_METATYPE(bool*)
Q_DECLARE_METATYPE(CAmount)
Q_DECLARE_METATYPE(SynchronizationState)
Q_DECLARE_METATYPE(uint256)

static void RegisterMetaTypes()
{
    // Register meta types used for QMetaObject::invokeMethod and Qt::QueuedConnection
    qRegisterMetaType<bool*>();
    qRegisterMetaType<SynchronizationState>();
  #ifdef ENABLE_WALLET
    qRegisterMetaType<WalletModel*>();
  #endif
    // Register typedefs (see https://doc.qt.io/qt-5/qmetatype.html#qRegisterMetaType)
    // IMPORTANT: if CAmount is no longer a typedef use the normal variant above (see https://doc.qt.io/qt-5/qmetatype.html#qRegisterMetaType-1)
    qRegisterMetaType<CAmount>("CAmount");
    qRegisterMetaType<size_t>("size_t");

    qRegisterMetaType<std::function<void()>>("std::function<void()>");
    qRegisterMetaType<QMessageBox::Icon>("QMessageBox::Icon");
}

static QString GetLangTerritory()
{
    QSettings settings;
    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = QLocale::system().name();
    // 2) Language from QSettings
    QString lang_territory_qsettings = settings.value("language", "").toString();
    if(!lang_territory_qsettings.isEmpty())
        lang_territory = lang_territory_qsettings;
    // 3) -lang command line argument
    lang_territory = QString::fromStdString(gArgs.GetArg("-lang", lang_territory.toStdString()));
    return lang_territory;
}

/** Set up translations */
static void initTranslations(QTranslator &qtTranslatorBase, QTranslator &qtTranslator, QTranslator &translatorBase, QTranslator &translator)
{
    // Remove old translators
    QApplication::removeTranslator(&qtTranslatorBase);
    QApplication::removeTranslator(&qtTranslator);
    QApplication::removeTranslator(&translatorBase);
    QApplication::removeTranslator(&translator);

    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = GetLangTerritory();

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    // Load e.g. bitcoin_de.qm (shortcut "de" needs to be defined in bitcoin.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        QApplication::installTranslator(&translatorBase);

    // Load e.g. bitcoin_de_DE.qm (shortcut "de_DE" needs to be defined in bitcoin.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        QApplication::installTranslator(&translator);
}

static bool InitSettings()
{
    if (!gArgs.GetSettingsPath()) {
        return true; // Do nothing if settings file disabled.
    }

    std::vector<std::string> errors;
    if (!gArgs.ReadSettingsFile(&errors)) {
        std::string error = QT_TRANSLATE_NOOP("bitcoin-core", "Settings file could not be read");
        std::string error_translated = QCoreApplication::translate("bitcoin-core", error.c_str()).toStdString();
        InitError(Untranslated(strprintf("%s:\n%s\n", error, MakeUnorderedList(errors))));

        QMessageBox messagebox(QMessageBox::Critical, PACKAGE_NAME, QString::fromStdString(strprintf("%s.", error_translated)), QMessageBox::Reset | QMessageBox::Abort);
        /*: Explanatory text shown on startup when the settings file cannot be read.
            Prompts user to make a choice between resetting or aborting. */
        messagebox.setInformativeText(QObject::tr("Do you want to reset settings to default values, or to abort without making changes?"));
        messagebox.setDetailedText(QString::fromStdString(MakeUnorderedList(errors)));
        messagebox.setTextFormat(Qt::PlainText);
        messagebox.setDefaultButton(QMessageBox::Reset);
        switch (messagebox.exec()) {
        case QMessageBox::Reset:
            break;
        case QMessageBox::Abort:
            return false;
        default:
            assert(false);
        }
    }

    errors.clear();
    if (!gArgs.WriteSettingsFile(&errors)) {
        std::string error = QT_TRANSLATE_NOOP("bitcoin-core", "Settings file could not be written");
        std::string error_translated = QCoreApplication::translate("bitcoin-core", error.c_str()).toStdString();
        InitError(Untranslated(strprintf("%s:\n%s\n", error, MakeUnorderedList(errors))));

        QMessageBox messagebox(QMessageBox::Critical, PACKAGE_NAME, QString::fromStdString(strprintf("%s.", error_translated)), QMessageBox::Ok);
        /*: Explanatory text shown on startup when the settings file could not be written.
            Prompts user to check that we have the ability to write to the file.
            Explains that the user has the option of running without a settings file.*/
        messagebox.setInformativeText(QObject::tr("A fatal error occurred. Check that settings file is writable, or try running with -nosettings."));
        messagebox.setDetailedText(QString::fromStdString(MakeUnorderedList(errors)));
        messagebox.setTextFormat(Qt::PlainText);
        messagebox.setDefaultButton(QMessageBox::Ok);
        messagebox.exec();
        return false;
    }
    return true;
}

/* qDebug() message handler --> debug.log */
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    Q_UNUSED(context);
    if (type == QtDebugMsg) {
        LogPrint(BCLog::QT, "GUI: %s\n", msg.toStdString());
    } else {
        LogPrintf("GUI: %s\n", msg.toStdString());
    }
}

static int qt_argc = 1;
static const char* qt_argv = "bitcoin-qt";

BitcoinApplication::BitcoinApplication():
    QApplication(qt_argc, const_cast<char **>(&qt_argv)),
    optionsModel(nullptr),
    clientModel(nullptr),
    window(nullptr),
    pollShutdownTimer(nullptr),
    returnValue(0),
    platformStyle(nullptr)
{
    // Qt runs setlocale(LC_ALL, "") on initialization.
    RegisterMetaTypes();
    setQuitOnLastWindowClosed(false);
}

void BitcoinApplication::setupPlatformStyle()
{
    // UI per-platform customization
    // This must be done inside the BitcoinApplication constructor, or after it, because
    // PlatformStyle::instantiate requires a QApplication
    std::string platformName;
    platformName = gArgs.GetArg("-uiplatform", BitcoinGUI::DEFAULT_UIPLATFORM);
    platformStyle = PlatformStyle::instantiate(QString::fromStdString(platformName));
    if (!platformStyle) // Fall back to "other" if specified name not found
        platformStyle = PlatformStyle::