#include "widgets/settingspages/ModerationPage.hpp"

#include "Application.hpp"
#include "controllers/logging/ChannelLoggingModel.hpp"
#include "controllers/moderationactions/ModerationAction.hpp"
#include "controllers/moderationactions/ModerationActionModel.hpp"
#include "singletons/Logging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"
#include "util/LayoutCreator.hpp"
#include "util/LoadPixmap.hpp"
#include "util/PostToThread.hpp"
#include "widgets/helper/EditableModelView.hpp"
#include "widgets/helper/IconDelegate.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTableView>
#include <QtConcurrent/QtConcurrent>

namespace chatterino {

qint64 dirSize(QString &dirPath)
{
    QDirIterator it(dirPath, QDirIterator::Subdirectories);
    qint64 size = 0;

    while (it.hasNext())
    {
        size += it.fileInfo().size();
        it.next();
    }

    return size;
}

QString formatSize(qint64 size)
{
    QStringList units = {"Bytes", "KB", "MB", "GB", "TB", "PB"};
    int i;
    double outputSize = size;
    for (i = 0; i < units.size() - 1; i++)
    {
        if (outputSize < 1024)
        {
            break;
        }
        outputSize = outputSize / 1024;
    }
    return QString("%0 %1").arg(outputSize, 0, 'f', 2).arg(units[i]);
}

QString fetchLogDirectorySize()
{
    QString logsDirectoryPath = getSettings()->logPath.getValue().isEmpty()
                                    ? getApp()->getPaths().messageLogDirectory
                                    : getSettings()->logPath;

    auto logsSize = dirSize(logsDirectoryPath);

    return QString("Your logs currently take up %1 of space")
        .arg(formatSize(logsSize));
}

ModerationPage::ModerationPage()
{
    LayoutCreator<ModerationPage> layoutCreator(this);

    auto tabs = layoutCreator.emplace<QTabWidget>();
    this->tabWidget_ = tabs.getElement();

    auto logs = tabs.appendTab(new QVBoxLayout, "Logs");
    {
        QCheckBox *enableLogging = this->createCheckBox(
            "Enable logging", getSettings()->enableLogging);
        logs.append(enableLogging);

        auto logsPathLabel = logs.emplace<QLabel>();

        // Logs (copied from LoggingMananger)
        getSettings()->logPath.connect(
            [logsPathLabel](const QString &logPath, auto) mutable {
                QString pathOriginal =
                    logPath.isEmpty() ? getApp()->getPaths().messageLogDirectory
                                      : logPath;

                QString pathShortened =
                    "Logs are saved at <a href=\"file:///" + pathOriginal +
                    R"("><span style="color: white;">)" +
                    shortenString(pathOriginal, 50) + "</span></a>";

                logsPathLabel->setText(pathShortened);
                logsPathLabel->setToolTip(pathOriginal);
            });

        logsPathLabel->setTextFormat(Qt::RichText);
        logsPathLabel->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                               Qt::LinksAccessibleByKeyboard);
        logsPathLabel->setOpenExternalLinks(true);

        auto buttons = logs.emplace<QHBoxLayout>().withoutMargin();

        // Select and Reset
        auto selectDir = buttons.emplace<QPushButton>("Select log directory ");
        auto resetDir = buttons.emplace<QPushButton>("Reset");

        getSettings()->logPath.connect(
            [element = resetDir.getElement()](const QString &path) {
                element->setEnabled(!path.isEmpty());
            });

        buttons->addStretch();

        // Show how big (size-wise) the logs are
        auto logsPathSizeLabel = logs.emplace<QLabel>();
        logsPathSizeLabel->setText(QtConcurrent::run([] {
                                       return fetchLogDirectorySize();
                                   }).result());

        // Select event
        QObject::connect(
            selectDir.getElement(), &QPushButton::clicked, this,
            [this, logsPathSizeLabel]() mutable {
                auto dirName = QFileDialog::getExistingDirectory(this);

                getSettings()->logPath = dirName;

                // Refresh: Show how big (size-wise) the logs are
                logsPathSizeLabel->setText(QtConcurrent::run([] {
                                               return fetchLogDirectorySize();
                                           }).result());
            });

        buttons->addSpacing(16);

        // Reset custom logpath
        QObject::connect(
            resetDir.getElement(), &QPushButton::clicked, this,
            [logsPathSizeLabel]() mutable {
                getSettings()->logPath = "";

                // Refresh: Show how big (size-wise) the logs are
                logsPathSizeLabel->setText(QtConcurrent::run([] {
                                               return fetchLogDirectorySize();
                                           }).result());
            });

        auto logsTimestampFormatLayout =
            logs.emplace<QHBoxLayout>().withoutMargin();
        auto logsTimestampFormatLabel =
            logsTimestampFormatLayout.emplace<QLabel>();
        logsTimestampFormatLabel->setText(
            QString("Log file timestamp format: "));

        QComboBox *logTimestampFormat = this->createComboBox(
            {"Disable", "h:mm", "hh:mm", "h:mm a", "hh:mm a", "h:mm:ss",
             "hh:mm:ss", "h:mm:ss a", "hh:mm:ss a", "h:mm:ss.zzz",
             "h:mm:ss.zzz a", "hh:mm:ss.zzz", "hh:mm:ss.zzz a"},
            getSettings()->logTimestampFormat);
        logTimestampFormat->setToolTip("a = am/pm, zzz = milliseconds");
        logsTimestampFormatLayout.append(logTimestampFormat);

        SettingWidget::checkbox("Use Twitch's timestamps",
                                getSettings()->tryUseTwitchTimestamps)
            ->setTooltip(
                "Try to use Twitch's timestamp (the time when the message was "
                "received by Twitch's chat server), rather than your "
                "computer's local timestamp.\nNote that using this setting can "
                "result in out-of-order timestamps in the log files, and that "
                "if Twitch's timestamp was unavailable for a message, it will "
                "fall back to your computer's local timestamp.")
            ->conditionallyEnabledBy(getSettings()->enableLogging)
            ->addToLayout(logs->layout());

        QCheckBox *onlyLogListedChannels =
            this->createCheckBox("Only log channels listed below",
                                 getSettings()->onlyLogListedChannels);

        onlyLogListedChannels->setEnabled(getSettings()->enableLogging);
        logs.append(onlyLogListedChannels);

        auto *separatelyStoreStreamLogs =
            this->createCheckBox("Store live stream logs as separate files",
                                 getSettings()->separatelyStoreStreamLogs);

        separatelyStoreStreamLogs->setEnabled(getSettings()->enableLogging);
        logs.append(separatelyStoreStreamLogs);

        // Select event
        QObject::connect(
            enableLogging, &QCheckBox::stateChanged, this,
            [enableLogging, onlyLogListedChannels,
             separatelyStoreStreamLogs]() mutable {
                onlyLogListedChannels->setEnabled(enableLogging->isChecked());
                separatelyStoreStreamLogs->setEnabled(
                    getSettings()->enableLogging);
            });

        EditableModelView *view =
            logs.emplace<EditableModelView>(
                    (new ChannelLoggingModel(nullptr))
                        ->initialized(&getSettings()->loggedChannels))
                .getElement();

        view->setTitles({"Twitch channels"});
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Fixed);
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);

        // We can safely ignore this signal connection since we own the view
        std::ignore = view->addButtonPressed.connect([] {
            getSettings()->loggedChannels.append(ChannelLog("channel"));
        });

    }  // logs end

    auto modMode = tabs.appendTab(new QVBoxLayout, "Moderation buttons");
    {
        // clang-format off
        auto label = modMode.emplace<QLabel>(
            "Moderation mode is enabled by clicking <img width='18' height='18' src=':/buttons/modModeDisabled.png'> in a channel that you moderate.<br><br>"
            "Moderation buttons can be bound to chat commands such as \"/ban {user.name}\", \"/timeout {user.name} 1000\", \"/w someusername !report {user.name} was bad in channel {channel.name}\" or any other custom text commands.<br>"
            "For deleting messages use /delete {msg.id}.<br><br>"
            "More information can be found <a href='https://wiki.chatterino.com/Moderation/#moderation-mode'>here</a>.");
        label->setOpenExternalLinks(true);
        label->setWordWrap(true);
        label->setStyleSheet("color: #bbb");
        // clang-format on

        //        auto form = modMode.emplace<QFormLayout>();
        //        {
        //            form->addRow("Action on timed out messages
        //            (unimplemented):",
        //                         this->createComboBox({"Disable", "Hide"},
        //                         getSettings()->timeoutAction));
        //        }

        EditableModelView *view =
            modMode
                .emplace<EditableModelView>(
                    (new ModerationActionModel(nullptr))
                        ->initialized(&getSettings()->moderationActions))
                .getElement();

        view->setTitles({"Action", "Icon"});
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Fixed);
        view->getTableView()->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        view->getTableView()->setItemDelegateForColumn(
            ModerationActionModel::Column::Icon, new IconDelegate(view));
        QObject::connect(
            view->getTableView(), &QTableView::clicked,
            [this, view](const QModelIndex &clicked) {
                if (clicked.column() == ModerationActionModel::Column::Icon)
                {
                    auto fileUrl = QFileDialog::getOpenFileUrl(
                        this, "Open Image", QUrl(),
                        "Image Files (*.png *.jpg *.jpeg)");
                    view->getModel()->setData(clicked, fileUrl, Qt::UserRole);
                    view->getModel()->setData(clicked, fileUrl.fileName(),
                                              Qt::DisplayRole);
                    // Clear the icon if the user canceled the dialog
                    if (fileUrl.isEmpty())
                    {
                        view->getModel()->setData(clicked, QVariant(),
                                                  Qt::DecorationRole);
                    }
                    else
                    {
                        // QPointer will be cleared when view is destroyed
                        QPointer<EditableModelView> viewtemp = view;

                        loadPixmapFromUrl(
                            {fileUrl.toString()},
                            [clicked, view = viewtemp](const QPixmap &pixmap) {
                                postToThread([clicked, view, pixmap]() {
                                    if (view.isNull())
                                    {
                                        return;
                                    }

                                    view->getModel()->setData(
                                        clicked, pixmap, Qt::DecorationRole);
                                });
                            });
                    }
                }
            });

        // We can safely ignore this signal connection since we own the view
        std::ignore = view->addButtonPressed.connect([] {
            getSettings()->moderationActions.append(
                ModerationAction("/timeout {user.name} 300"));
        });
    }

    this->addModerationButtonSettings(tabs.getElement());

    // ---- misc
    this->itemsChangedTimer_.setSingleShot(true);
}

void ModerationPage::addModerationButtonSettings(QTabWidget *tabs)
{
    auto timeoutLayout =
        LayoutCreator{tabs}.appendTab(new QVBoxLayout, "User Timeout Buttons");
    auto texts = timeoutLayout.emplace<QVBoxLayout>().withoutMargin();
    {
        auto infoLabel = texts.emplace<QLabel>();
        infoLabel->setText(
            "Customize the timeout buttons in the user popup (accessible "
            "through clicking a username).\nUse seconds (s), "
            "minutes (m), hours (h), days (d) or weeks (w).");

        infoLabel->setAlignment(Qt::AlignCenter);

        auto maxLabel = texts.emplace<QLabel>();
        maxLabel->setText("(maximum timeout duration = 2 w)");
        maxLabel->setAlignment(Qt::AlignCenter);
    }
    texts->setContentsMargins(0, 0, 0, 15);
    texts->setSizeConstraint(QLayout::SetMaximumSize);

    const auto valueChanged = [=, this] {
        const auto index = QObject::sender()->objectName().toInt();

        auto *const line = this->durationInputs_[index];
        const auto duration = line->text().toInt();
        const auto unit = this->unitInputs_[index]->currentText();

        // safety mechanism for setting days and weeks
        if (unit == "d" && duration > 14)
        {
            line->setText("14");
            return;
        }
        else if (unit == "w" && duration > 2)
        {
            line->setText("2");
            return;
        }

        auto timeouts = getSettings()->timeoutButtons.getValue();
        timeouts[index] = TimeoutButton{unit, duration};
        getSettings()->timeoutButtons.setValue(timeouts);
    };

    // build one line for each customizable button
    auto i = 0;
    for (const auto &tButton : getSettings()->timeoutButtons.getValue())
    {
        const auto buttonNumber = QString::number(i);
        auto timeout = timeoutLayout.emplace<QHBoxLayout>().withoutMargin();

        auto buttonLabel = timeout.emplace<QLabel>();
        buttonLabel->setText(QString("Button %1: ").arg(++i));

        auto *lineEditDurationInput = new QLineEdit();
        lineEditDurationInput->setObjectName(buttonNumber);
        lineEditDurationInput->setValidator(new QIntValidator(1, 99, this));
        lineEditDurationInput->setText(QString::number(tButton.second));
        lineEditDurationInput->setAlignment(Qt::AlignRight);
        lineEditDurationInput->setMaximumWidth(30);
        timeout.append(lineEditDurationInput);

        auto *timeoutDurationUnit = new QComboBox();
        timeoutDurationUnit->setObjectName(buttonNumber);
        timeoutDurationUnit->addItems({"s", "m", "h", "d", "w"});
        timeoutDurationUnit->setCurrentText(tButton.first);
        timeout.append(timeoutDurationUnit);

        QObject::connect(lineEditDurationInput, &QLineEdit::textChanged, this,
                         valueChanged);

        QObject::connect(timeoutDurationUnit, &QComboBox::currentTextChanged,
                         this, valueChanged);

        timeout->addStretch();

        this->durationInputs_.push_back(lineEditDurationInput);
        this->unitInputs_.push_back(timeoutDurationUnit);

        timeout->setContentsMargins(40, 0, 0, 0);
        timeout->setSizeConstraint(QLayout::SetMaximumSize);
    }
    timeoutLayout->addStretch();
}

void ModerationPage::selectModerationActions()
{
    this->tabWidget_->setCurrentIndex(1);
}

}  // namespace chatterino
