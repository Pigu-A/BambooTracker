#include "mainwindow.hpp"
#include "ui_mainwindow.h"
#include <fstream>
#include <QString>
#include <QLineEdit>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QRegularExpression>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QProgressDialog>
#include <QRect>
#include <QDesktopWidget>
#include <QAudioDeviceInfo>
#include "jam_manager.hpp"
#include "song.hpp"
#include "track.hpp"
#include "instrument.hpp"
#include "bank.hpp"
#include "bank_io.hpp"
#include "version.hpp"
#include "gui/command/commands_qt.hpp"
#include "gui/instrument_editor/instrument_editor_fm_form.hpp"
#include "gui/instrument_editor/instrument_editor_ssg_form.hpp"
#include "gui/module_properties_dialog.hpp"
#include "gui/groove_settings_dialog.hpp"
#include "gui/configuration_dialog.hpp"
#include "gui/comment_edit_dialog.hpp"
#include "gui/wave_export_settings_dialog.hpp"
#include "gui/vgm_export_settings_dialog.hpp"
#include "gui/instrument_selection_dialog.hpp"
#include "gui/s98_export_settings_dialog.hpp"
#include "gui/configuration_handler.hpp"
#include "chips/scci/SCCIDefines.h"

MainWindow::MainWindow(QString filePath, QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow),
	config_(std::make_shared<Configuration>()),
	palette_(std::make_shared<ColorPalette>()),
	comStack_(std::make_shared<QUndoStack>(this)),
	scciDll_(std::make_unique<QLibrary>("scci")),
	instForms_(std::make_shared<InstrumentFormManager>()),
	isModifiedForNotCommand_(false),
	isEditedPattern_(true),
	isEditedOrder_(false),
	isSelectedPO_(false)
{
	ui->setupUi(this);

	ConfigurationHandler::loadConfiguration(config_);
	bt_ = std::make_shared<BambooTracker>(config_);

	if (config_->getMainWindowX() == -1) {	// When unset
		QRect rec = geometry();
		rec.moveCenter(QApplication::desktop()->availableGeometry().center());
		setGeometry(rec);
		config_->setMainWindowX(x());
		config_->setMainWindowY(y());
	}
	else {
		move(config_->getMainWindowX(), config_->getMainWindowY());
	}
	resize(config_->getMainWindowWidth(), config_->getMainWindowHeight());
	if (config_->getMainWindowMaximized()) showMaximized();
	ui->actionFollow_Mode->setChecked(config_->getFollowMode());
	bt_->setFollowPlay(config_->getFollowMode());

	/* Command stack */
	QObject::connect(comStack_.get(), &QUndoStack::indexChanged,
					 this, [&](int idx) {
		setWindowModified(idx || isModifiedForNotCommand_);
		ui->actionUndo->setEnabled(comStack_->canUndo());
		ui->actionRedo->setEnabled(comStack_->canRedo());
	});

	/* Audio stream */
	bool savedDeviceExists = false;
	for (QAudioDeviceInfo audioDevice : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)) {
		if (audioDevice.deviceName().toUtf8().toStdString() == config_->getSoundDevice()) {
			savedDeviceExists = true;
			break;
		}
	}
	if (!savedDeviceExists) {
		QString sndDev = QAudioDeviceInfo::defaultOutputDevice().deviceName();
		config_->setSoundDevice(sndDev.toUtf8().toStdString());
	}
	stream_ = std::make_shared<AudioStream>(bt_->getStreamRate(),
											bt_->getStreamDuration(),
											bt_->getModuleTickFrequency(),
											QString::fromUtf8(config_->getSoundDevice().c_str(),
															  config_->getSoundDevice().length()));
	QObject::connect(stream_.get(), &AudioStream::streamInterrupted,
					 this, &MainWindow::onNewTickSignaled, Qt::DirectConnection);
	QObject::connect(stream_.get(), &AudioStream::bufferPrepared,
					 this, [&](int16_t *container, size_t nSamples) {
		bt_->getStreamSamples(container, nSamples);
	}, Qt::DirectConnection);
	if (config_->getUseSCCI()) {
		stream_->stop();
		/*
		timer_ = std::make_unique<Timer>();
		timer_->setInterval(1000 / bt_->getModuleTickFrequency());
		timer_->setFunction([&]{ onNewTickSignaled(); });
		*/
		timer_ = std::make_unique<QTimer>(this);
		timer_->setTimerType(Qt::PreciseTimer);
		timer_->setInterval(1000 / bt_->getModuleTickFrequency());
		timer_->setSingleShot(false);
		QObject::connect(timer_.get(), &QTimer::timeout, this, &MainWindow::onNewTickSignaled);

		scciDll_->load();
		if (scciDll_->isLoaded()) {
			SCCIFUNC getSoundInterfaceManager = reinterpret_cast<SCCIFUNC>(
													scciDll_->resolve("getSoundInterfaceManager"));
			bt_->useSCCI(getSoundInterfaceManager ? getSoundInterfaceManager() : nullptr);
		}
		else {
			bt_->useSCCI(nullptr);
		}

		timer_->start();
	}
	else {
		bt_->useSCCI(nullptr);
		stream_->start();
	}

	/* Sub tool bar */
	auto octLab = new QLabel(tr("Octave"));
	octLab->setMargin(6);
	ui->subToolBar->addWidget(octLab);
	octave_ = new QSpinBox();
	octave_->setMinimum(0);
	octave_->setMaximum(7);
	octave_->setValue(bt_->getCurrentOctave());
	auto octFunc = [&](int octave) { bt_->setCurrentOctave(octave); };
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(octave_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, octFunc);
	ui->subToolBar->addWidget(octave_);
	ui->subToolBar->addSeparator();
	ui->subToolBar->addAction(ui->actionFollow_Mode);
	ui->subToolBar->addSeparator();
	auto hlLab = new QLabel(tr("Step highlight"));
	hlLab->setMargin(6);
	ui->subToolBar->addWidget(hlLab);
	highlight_ = new QSpinBox();
	highlight_->setMinimum(1);
	highlight_->setMaximum(256);
	highlight_->setValue(8);
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(highlight_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int count) {
		bt_->setModuleStepHighlightDistance(count);
		ui->patternEditor->setPatternHighlightCount(count);
		ui->patternEditor->update();
	});
	ui->subToolBar->addWidget(highlight_);

	/* Module settings */
	QObject::connect(ui->modTitleLineEdit, &QLineEdit::textEdited,
					 this, [&](QString str) {
		bt_->setModuleTitle(str.toUtf8().toStdString());
		setModifiedTrue();
		setWindowTitle();
	});
	QObject::connect(ui->authorLineEdit, &QLineEdit::textEdited,
					 this, [&](QString str) {
		bt_->setModuleAuthor(str.toUtf8().toStdString());
		setModifiedTrue();
	});
	QObject::connect(ui->copyrightLineEdit, &QLineEdit::textEdited,
					 this, [&](QString str) {
		bt_->setModuleCopyright(str.toUtf8().toStdString());
		setModifiedTrue();
	});
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->tickFreqSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int freq) {
		if (freq != bt_->getModuleTickFrequency()) {
			bt_->setModuleTickFrequency(freq);
			stream_->setInturuption(freq);
			if (timer_) timer_->setInterval(1000 / freq);
			statusIntr_->setText(QString::number(freq) + QString("Hz"));
			setModifiedTrue();
		}
	});
	QObject::connect(ui->modSetDialogOpenToolButton, &QToolButton::clicked,
					 this, &MainWindow::on_actionModule_Properties_triggered);

	/* Edit settings */
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->editableStepSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int n) {
		ui->patternEditor->setEditableStep(n);
		config_->setEditableStep(n);
	});
	ui->editableStepSpinBox->setValue(config_->getEditableStep());
	ui->patternEditor->setEditableStep(config_->getEditableStep());

	/* Song number */
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->songNumSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int num) {
		bt_->setCurrentSongNumber(num);
		loadSong();
	});

	/* Song settings */
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->tempoSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int tempo) {
		int curSong = bt_->getCurrentSongNumber();
		if (tempo != bt_->getSongTempo(curSong)) {
			bt_->setSongTempo(curSong, tempo);
			setModifiedTrue();
		}
	});
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->speedSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int speed) {
		int curSong = bt_->getCurrentSongNumber();
		if (speed != bt_->getSongSpeed(curSong)) {
			bt_->setSongSpeed(curSong, speed);
			setModifiedTrue();
		}
	});
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->patternSizeSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int size) {
		bt_->setDefaultPatternSize(bt_->getCurrentSongNumber(), size);
		ui->patternEditor->onDefaultPatternSizeChanged();
		setModifiedTrue();
	});
	// Leave Before Qt5.7.0 style due to windows xp
	QObject::connect(ui->grooveSpinBox, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
					 this, [&](int n) {
		bt_->setSongGroove(bt_->getCurrentSongNumber(), n);
		setModifiedTrue();
	});

	/* Instrument list */
	ui->instrumentListWidget->setStyleSheet(
				QString(
					"QListWidget {"
					"	color: rgba(%1, %2, %3, %4);"
					"	background: rgba(%5, %6, %7, %8);"
					"}"
					).arg(palette_->ilistTextColor.red()).arg(palette_->ilistTextColor.green())
				.arg(palette_->ilistTextColor.blue()).arg(palette_->ilistTextColor.alpha())
				.arg(palette_->ilistBackColor.red()).arg(palette_->ilistBackColor.green())
				.arg(palette_->ilistBackColor.blue()).arg(palette_->ilistBackColor.alpha())
				+ QString(
					"QListWidget::item:hover {"
					"	color: rgba(%1, %2, %3, %4);"
					"	background: rgba(%5, %6, %7, %8);"
					"}"
					).arg(palette_->ilistHovTextColor.red()).arg(palette_->ilistHovTextColor.green())
				.arg(palette_->ilistHovTextColor.blue()).arg(palette_->ilistHovTextColor.alpha())
				.arg(palette_->ilistHovBackColor.red()).arg(palette_->ilistHovBackColor.green())
				.arg(palette_->ilistHovBackColor.blue()).arg(palette_->ilistHovBackColor.alpha())
				+ QString(
					"QListWidget::item:selected {"
					"	color: rgba(%1, %2, %3, %4);"
					"	background: rgba(%5, %6, %7, %8);"
					"}"
					).arg(palette_->ilistSelTextColor.red()).arg(palette_->ilistSelTextColor.green())
				.arg(palette_->ilistSelTextColor.blue()).arg(palette_->ilistSelTextColor.alpha())
				.arg(palette_->ilistSelBackColor.red()).arg(palette_->ilistSelBackColor.green())
				.arg(palette_->ilistSelBackColor.blue()).arg(palette_->ilistSelBackColor.alpha())
				+ QString(
					"QListWidget::item:selected:hover {"
					"	color: rgba(%1, %2, %3, %4);"
					"	background: rgba(%5, %6, %7, %8);"
					"}"
					).arg(palette_->ilistHovSelTextColor.red()).arg(palette_->ilistHovSelTextColor.green())
				.arg(palette_->ilistHovSelTextColor.blue()).arg(palette_->ilistHovSelTextColor.alpha())
				.arg(palette_->ilistHovSelBackColor.red()).arg(palette_->ilistHovSelBackColor.green())
				.arg(palette_->ilistHovSelBackColor.blue()).arg(palette_->ilistHovSelBackColor.alpha())
				);
	ui->instrumentListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	ui->instrumentListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
	// Set core data to editor when add insrument
	QObject::connect(ui->instrumentListWidget->model(), &QAbstractItemModel::rowsInserted,
					 this, &MainWindow::onInstrumentListWidgetItemAdded);
	auto instToolBar = new QToolBar();
	instToolBar->setIconSize(QSize(16, 16));
	instToolBar->addAction(ui->actionNew_Instrument);
	instToolBar->addAction(ui->actionRemove_Instrument);
	instToolBar->addAction(ui->actionClone_Instrument);
	instToolBar->addSeparator();
	instToolBar->addAction(ui->actionLoad_From_File);
	instToolBar->addAction(ui->actionSave_To_File);
	instToolBar->addSeparator();
	instToolBar->addAction(ui->actionEdit);
	ui->instrumentListGroupBox->layout()->addWidget(instToolBar);

	/* Pattern editor */
	ui->patternEditor->setCore(bt_);
	ui->patternEditor->setCommandStack(comStack_);
	ui->patternEditor->setConfiguration(config_);
	ui->patternEditor->setColorPallete(palette_);
	ui->patternEditor->installEventFilter(this);
	QObject::connect(ui->patternEditor, &PatternEditor::currentTrackChanged,
					 ui->orderList, &OrderListEditor::setCurrentTrack);
	QObject::connect(ui->patternEditor, &PatternEditor::currentOrderChanged,
					 ui->orderList, &OrderListEditor::setCurrentOrder);
	QObject::connect(ui->patternEditor, &PatternEditor::focusIn,
					 this, &MainWindow::updateMenuByPattern);
	QObject::connect(ui->patternEditor, &PatternEditor::focusOut,
					 this, &MainWindow::onPatternAndOrderFocusLost);
	QObject::connect(ui->patternEditor, &PatternEditor::selected,
					 this, &MainWindow::updateMenuByPatternAndOrderSelection);
	QObject::connect(ui->patternEditor, &PatternEditor::returnPressed,
					 this, [&] {
		if (bt_->isPlaySong()) stopPlaySong();
		else startPlaySong();
	});
	QObject::connect(ui->patternEditor, &PatternEditor::instrumentEntered,
					 this, [&](int num) {
		auto list = ui->instrumentListWidget;
		if (num != -1) {
			for (int i = 0; i < list->count(); ++i) {
				if (list->item(i)->data(Qt::UserRole).toInt() == num) {
					list->setCurrentRow(i);
					return ;
				}
			}
		}
	});
	QObject::connect(ui->patternEditor, &PatternEditor::effectEntered,
					 this, [&](QString text) { statusDetail_->setText(text); });

	/* Order List */
	ui->orderList->setCore(bt_);
	ui->orderList->setCommandStack(comStack_);
	ui->orderList->setConfiguration(config_);
	ui->orderList->setColorPallete(palette_);
	ui->orderList->installEventFilter(this);
	QObject::connect(ui->orderList, &OrderListEditor::currentTrackChanged,
					 ui->patternEditor, &PatternEditor::setCurrentTrack);
	QObject::connect(ui->orderList, &OrderListEditor::currentOrderChanged,
					 ui->patternEditor, &PatternEditor::setCurrentOrder);
	QObject::connect(ui->orderList, &OrderListEditor::orderEdited,
					 ui->patternEditor, &PatternEditor::onOrderListEdited);
	QObject::connect(ui->orderList, &OrderListEditor::focusIn,
					 this, &MainWindow::updateMenuByOrder);
	QObject::connect(ui->orderList, &OrderListEditor::focusOut,
					 this, &MainWindow::onPatternAndOrderFocusLost);
	QObject::connect(ui->orderList, &OrderListEditor::selected,
					 this, &MainWindow::updateMenuByPatternAndOrderSelection);
	QObject::connect(ui->orderList, &OrderListEditor::returnPressed,
					 this, [&] {
		if (bt_->isPlaySong()) stopPlaySong();
		else startPlaySong();
	});

	/* Status bar */
	statusDetail_ = new QLabel();
	statusStyle_ = new QLabel();
	statusInst_ = new QLabel();
	statusOctave_ = new QLabel();
	statusIntr_ = new QLabel();
	statusPlayPos_ = new QLabel();
	ui->statusBar->addWidget(statusDetail_, 5);
	ui->statusBar->addPermanentWidget(statusStyle_, 1);
	ui->statusBar->addPermanentWidget(statusInst_, 1);
	ui->statusBar->addPermanentWidget(statusOctave_, 1);
	ui->statusBar->addPermanentWidget(statusIntr_, 1);
	ui->statusBar->addPermanentWidget(statusPlayPos_, 1);
	statusOctave_->setText(tr("Octave: %1").arg(bt_->getCurrentOctave()));
	statusIntr_->setText(QString::number(bt_->getModuleTickFrequency()) + QString("Hz"));

	/* Clipboard */
	QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged,
					 this, [&]() {
		if (isEditedOrder_) updateMenuByOrder();
		else if (isEditedPattern_) updateMenuByPattern();
	});

	if (filePath == "") {
		loadModule();
	}
	else {
		try {
			bt_->loadModule(filePath.toLocal8Bit().toStdString());
			loadModule();

			config_->setWorkingDirectory(QFileInfo(filePath).dir().path().toStdString());
			isModifiedForNotCommand_ = false;
			setWindowModified(false);
		}
		catch (std::exception& e) {
			QMessageBox::critical(this, tr("Error"), e.what());
		}
	}
}

MainWindow::~MainWindow()
{
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (auto fmForm = qobject_cast<InstrumentEditorFMForm*>(watched)) {
		// Change current instrument by activating FM editor
		if (event->type() == QEvent::WindowActivate) {
			int row = findRowFromInstrumentList(fmForm->getInstrumentNumber());
			ui->instrumentListWidget->setCurrentRow(row);
			return false;
		}
		else if (event->type() == QEvent::Resize) {
			config_->setInstrumentFMWindowWidth(fmForm->width());
			config_->setInstrumentFMWindowHeight(fmForm->height());
			return false;
		}
	}

	if (auto ssgForm = qobject_cast<InstrumentEditorSSGForm*>(watched)) {
		// Change current instrument by activating SSG editor
		if (event->type() == QEvent::WindowActivate) {
			int row = findRowFromInstrumentList(ssgForm->getInstrumentNumber());
			ui->instrumentListWidget->setCurrentRow(row);
			return false;
		}
		else if (event->type() == QEvent::Resize) {
			config_->setInstrumentSSGWindowWidth(ssgForm->width());
			config_->setInstrumentSSGWindowHeight(ssgForm->height());
			return false;
		}
	}

	return false;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
	int key = event->key();

	/* Key check */
	QString seq = QKeySequence(event->modifiers() | event->key()).toString();
	if (seq == QKeySequence(QString::fromUtf8(config_->getOctaveUpKey().c_str(),
											  config_->getOctaveUpKey().length())).toString()) {
		changeOctave(true);
		return;
	}
	else if (seq == QKeySequence(QString::fromUtf8(config_->getOctaveDownKey().c_str(),
												   config_->getOctaveDownKey().length())).toString()) {
		changeOctave(false);
		return;
	}

	/* Pressed alt */
	if (event->modifiers().testFlag(Qt::AltModifier)) {
		switch (key) {
		case Qt::Key_O:		ui->orderList->setFocus();		return;
		case Qt::Key_P:		ui->patternEditor->setFocus();	return;
		}
	}

	/* General keys */
	if (!event->isAutoRepeat()) {
		// Musical keyboard
		switch (key) {
		case Qt::Key_Z:			bt_->jamKeyOn(JamKey::LOW_C);		break;
		case Qt::Key_S:			bt_->jamKeyOn(JamKey::LOW_CS);		break;
		case Qt::Key_X:			bt_->jamKeyOn(JamKey::LOW_D);		break;
		case Qt::Key_D:			bt_->jamKeyOn(JamKey::LOW_DS);		break;
		case Qt::Key_C:			bt_->jamKeyOn(JamKey::LOW_E);		break;
		case Qt::Key_V:			bt_->jamKeyOn(JamKey::LOW_F);		break;
		case Qt::Key_G:			bt_->jamKeyOn(JamKey::LOW_FS);		break;
		case Qt::Key_B:			bt_->jamKeyOn(JamKey::LOW_G);		break;
		case Qt::Key_H:			bt_->jamKeyOn(JamKey::LOW_GS);		break;
		case Qt::Key_N:			bt_->jamKeyOn(JamKey::LOW_A);		break;
		case Qt::Key_J:			bt_->jamKeyOn(JamKey::LOW_AS);		break;
		case Qt::Key_M:			bt_->jamKeyOn(JamKey::LOW_B);		break;
		case Qt::Key_Comma:		bt_->jamKeyOn(JamKey::LOW_C_H);		break;
		case Qt::Key_L:			bt_->jamKeyOn(JamKey::LOW_CS_H);	break;
		case Qt::Key_Period:	bt_->jamKeyOn(JamKey::LOW_D_H);		break;
		case Qt::Key_Q:			bt_->jamKeyOn(JamKey::HIGH_C);		break;
		case Qt::Key_2:			bt_->jamKeyOn(JamKey::HIGH_CS);		break;
		case Qt::Key_W:			bt_->jamKeyOn(JamKey::HIGH_D);		break;
		case Qt::Key_3:			bt_->jamKeyOn(JamKey::HIGH_DS);		break;
		case Qt::Key_E:			bt_->jamKeyOn(JamKey::HIGH_E);		break;
		case Qt::Key_R:			bt_->jamKeyOn(JamKey::HIGH_F);		break;
		case Qt::Key_5:			bt_->jamKeyOn(JamKey::HIGH_FS);		break;
		case Qt::Key_T:			bt_->jamKeyOn(JamKey::HIGH_G);		break;
		case Qt::Key_6:			bt_->jamKeyOn(JamKey::HIGH_GS);		break;
		case Qt::Key_Y:			bt_->jamKeyOn(JamKey::HIGH_A);		break;
		case Qt::Key_7:			bt_->jamKeyOn(JamKey::HIGH_AS);		break;
		case Qt::Key_U:			bt_->jamKeyOn(JamKey::HIGH_B);		break;
		case Qt::Key_I:			bt_->jamKeyOn(JamKey::HIGH_C_H);	break;
		case Qt::Key_9:			bt_->jamKeyOn(JamKey::HIGH_CS_H);	break;
		case Qt::Key_O:			bt_->jamKeyOn(JamKey::HIGH_D_H);	break;
		default:	break;
		}
	}
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
	int key = event->key();

	if (!event->isAutoRepeat()) {
		// Musical keyboard
		switch (key) {
		case Qt::Key_Z:			bt_->jamKeyOff(JamKey::LOW_C);		break;
		case Qt::Key_S:			bt_->jamKeyOff(JamKey::LOW_CS);		break;
		case Qt::Key_X:			bt_->jamKeyOff(JamKey::LOW_D);		break;
		case Qt::Key_D:			bt_->jamKeyOff(JamKey::LOW_DS);		break;
		case Qt::Key_C:			bt_->jamKeyOff(JamKey::LOW_E);		break;
		case Qt::Key_V:			bt_->jamKeyOff(JamKey::LOW_F);		break;
		case Qt::Key_G:			bt_->jamKeyOff(JamKey::LOW_FS);		break;
		case Qt::Key_B:			bt_->jamKeyOff(JamKey::LOW_G);		break;
		case Qt::Key_H:			bt_->jamKeyOff(JamKey::LOW_GS);		break;
		case Qt::Key_N:			bt_->jamKeyOff(JamKey::LOW_A);		break;
		case Qt::Key_J:			bt_->jamKeyOff(JamKey::LOW_AS);		break;
		case Qt::Key_M:			bt_->jamKeyOff(JamKey::LOW_B);		break;
		case Qt::Key_Comma:		bt_->jamKeyOff(JamKey::LOW_C_H);	break;
		case Qt::Key_L:			bt_->jamKeyOff(JamKey::LOW_CS_H);	break;
		case Qt::Key_Period:	bt_->jamKeyOff(JamKey::LOW_D_H);	break;
		case Qt::Key_Q:			bt_->jamKeyOff(JamKey::HIGH_C);		break;
		case Qt::Key_2:			bt_->jamKeyOff(JamKey::HIGH_CS);	break;
		case Qt::Key_W:			bt_->jamKeyOff(JamKey::HIGH_D);		break;
		case Qt::Key_3:			bt_->jamKeyOff(JamKey::HIGH_DS);	break;
		case Qt::Key_E:			bt_->jamKeyOff(JamKey::HIGH_E);		break;
		case Qt::Key_R:			bt_->jamKeyOff(JamKey::HIGH_F);		break;
		case Qt::Key_5:			bt_->jamKeyOff(JamKey::HIGH_FS);	break;
		case Qt::Key_T:			bt_->jamKeyOff(JamKey::HIGH_G);		break;
		case Qt::Key_6:			bt_->jamKeyOff(JamKey::HIGH_GS);	break;
		case Qt::Key_Y:			bt_->jamKeyOff(JamKey::HIGH_A);		break;
		case Qt::Key_7:			bt_->jamKeyOff(JamKey::HIGH_AS);	break;
		case Qt::Key_U:			bt_->jamKeyOff(JamKey::HIGH_B);		break;
		case Qt::Key_I:			bt_->jamKeyOff(JamKey::HIGH_C_H);	break;
		case Qt::Key_9:			bt_->jamKeyOff(JamKey::HIGH_CS_H);	break;
		case Qt::Key_O:			bt_->jamKeyOff(JamKey::HIGH_D_H);	break;
		default:	break;
		}
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
	auto mime = event->mimeData();
	if (mime->hasUrls() && mime->urls().length() == 1
			&& mime->urls().first().toLocalFile().endsWith(".btm"))
		event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
	if (isWindowModified()) {
		auto modTitleStd = bt_->getModuleTitle();
		QString modTitle = QString::fromUtf8(modTitleStd.c_str(), modTitleStd.length());
		if (modTitle.isEmpty()) modTitle = tr("Untitled");
		QMessageBox dialog(QMessageBox::Warning,
						   "BambooTracker",
						   tr("Save changes to %1?").arg(modTitle),
						   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
		switch (dialog.exec()) {
		case QMessageBox::Yes:
			if (!on_actionSave_triggered()) return;
			break;
		case QMessageBox::No:
			break;
		case QMessageBox::Cancel:
			return;
		default:
			break;
		}
	}

	bt_->stopPlaySong();
	lockControls(false);

	try {
		bt_->loadModule(event->mimeData()->urls().first().toLocalFile().toLocal8Bit().toStdString());
		loadModule();
		isModifiedForNotCommand_ = false;
		setWindowModified(false);
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
	}
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);

	if (!isMaximized()) {	// Check previous size
		config_->setMainWindowWidth(event->oldSize().width());
		config_->setMainWindowHeight(event->oldSize().height());
	}
}

void MainWindow::moveEvent(QMoveEvent* event)
{
	QWidget::moveEvent(event);

	if (!isMaximized()) {	// Check previous position
		config_->setMainWindowX(event->oldPos().x());
		config_->setMainWindowY(event->oldPos().y());
	}
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (isWindowModified()) {
		auto modTitleStd = bt_->getModuleTitle();
		QString modTitle = QString::fromUtf8(modTitleStd.c_str(), modTitleStd.length());
		if (modTitle.isEmpty()) modTitle = tr("Untitled");
		QMessageBox dialog(QMessageBox::Warning,
						   "BambooTracker",
						   tr("Save changes to %1?").arg(modTitle),
						   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
		switch (dialog.exec()) {
		case QMessageBox::Yes:
			if (!on_actionSave_triggered()) return;
			break;
		case QMessageBox::No:
			break;
		case QMessageBox::Cancel:
			event->ignore();
			return;
		default:
			break;
		}
	}

	if (isMaximized()) {
		config_->setMainWindowMaximized(true);
	}
	else {
		config_->setMainWindowMaximized(false);
		config_->setMainWindowWidth(width());
		config_->setMainWindowHeight(height());
		config_->setMainWindowX(x());
		config_->setMainWindowY(y());
	}
	config_->setFollowMode(bt_->isFollowPlay());

	instForms_->closeAll();
	ConfigurationHandler::saveConfiguration(config_);

	event->accept();
}

/********** Instrument list **********/
void MainWindow::addInstrument()
{
	switch (bt_->getCurrentTrackAttribute().source) {
	case SoundSource::FM:
	case SoundSource::SSG:
	{
		auto& list = ui->instrumentListWidget;

		int num = bt_->findFirstFreeInstrumentNumber();
		QString name = tr("Instrument %1").arg(num);
		bt_->addInstrument(num, name.toUtf8().toStdString());

		TrackAttribute attrib = bt_->getCurrentTrackAttribute();
		comStack_->push(new AddInstrumentQtCommand(list, num, name, attrib.source, instForms_));
		break;
	}
	case SoundSource::DRUM:
		break;
	}
}

void MainWindow::removeInstrument(int row)
{
	auto& list = ui->instrumentListWidget;
	int num = list->item(row)->data(Qt::UserRole).toInt();

	bt_->removeInstrument(num);
	comStack_->push(new RemoveInstrumentQtCommand(list, num, row, instForms_));
}

void MainWindow::editInstrument()
{
	auto item = ui->instrumentListWidget->currentItem();
	int num = item->data(Qt::UserRole).toInt();
	instForms_->showForm(num);
}

int MainWindow::findRowFromInstrumentList(int instNum)
{
	auto& list = ui->instrumentListWidget;
	int row = 0;
	for (; row < list->count(); ++row) {
		auto item = list->item(row);
		if (item->data(Qt::UserRole).toInt() == instNum) break;
	}
	return row;
}

void MainWindow::editInstrumentName()
{
	auto list = ui->instrumentListWidget;
	auto item = list->currentItem();
	int num = item->data(Qt::UserRole).toInt();
	QString oldName = instForms_->getFormInstrumentName(num);
	auto line = new QLineEdit(oldName);

	QObject::connect(line, &QLineEdit::editingFinished,
					 this, [&, item, list, num, oldName] {
		QString newName = qobject_cast<QLineEdit*>(list->itemWidget(item))->text();
		list->removeItemWidget(item);
		bt_->setInstrumentName(num, newName.toUtf8().toStdString());
		int row = findRowFromInstrumentList(num);
		comStack_->push(new ChangeInstrumentNameQtCommand(list, num, row, instForms_, oldName, newName));
	});

	ui->instrumentListWidget->setItemWidget(item, line);

	line->selectAll();
	line->setFocus();
}

void MainWindow::cloneInstrument()
{
	int num = bt_->findFirstFreeInstrumentNumber();
	if (num == -1) return;

	int refNum = ui->instrumentListWidget->currentItem()->data(Qt::UserRole).toInt();
	// KEEP CODE ORDER //
	bt_->cloneInstrument(num, refNum);
	comStack_->push(new CloneInstrumentQtCommand(
						ui->instrumentListWidget, num, refNum, instForms_));
	//----------//
}

void MainWindow::deepCloneInstrument()
{
	int num = bt_->findFirstFreeInstrumentNumber();
	if (num == -1) return;

	int refNum = ui->instrumentListWidget->currentItem()->data(Qt::UserRole).toInt();
	// KEEP CODE ORDER //
	bt_->deepCloneInstrument(num, refNum);
	comStack_->push(new DeepCloneInstrumentQtCommand(
						ui->instrumentListWidget, num, refNum, instForms_));
	//----------//
}

void MainWindow::loadInstrument()
{
	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getOpenFileName(this, tr("Open instrument"), (dir.isEmpty() ? "./" : dir),
												"BambooTracker instrument (*.bti);;"
												"DefleMask preset (*.dmp);;"
												"TFM Music Maker instrument (*.tfi);;"
												"VGM Music Maker instrument (*.vgi);;"
												"WOPN instrument (*.opni);;"
												"Gens KMod dump (*.y12);;"
												"MVSTracker instrument (*.ins)");
	if (file.isNull()) return;

	int n = bt_->findFirstFreeInstrumentNumber();
	if (n == -1) QMessageBox::critical(this, tr("Error"), tr("Failed to load instrument."));

	try {
		bt_->loadInstrument(file.toLocal8Bit().toStdString(), n);
		auto inst = bt_->getInstrument(n);
		auto name = inst->getName();
		comStack_->push(new AddInstrumentQtCommand(ui->instrumentListWidget, n,
												   QString::fromUtf8(name.c_str(), name.length()),
												   inst->getSoundSource(), instForms_));
		config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
	}
}

void MainWindow::saveInstrument()
{
	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getSaveFileName(this, tr("Save instrument"), (dir.isEmpty() ? "./" : dir),
												"BambooTracker instrument file (*.bti)");
	if (file.isNull()) return;
	if (!file.endsWith(".bti")) file += ".bti";	// For linux

	try {
		bt_->saveInstrument(file.toLocal8Bit().toStdString(),
							ui->instrumentListWidget->currentItem()->data(Qt::UserRole).toInt());
		config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
	}
}

void MainWindow::importInstrumentsFromBank()
{
	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getOpenFileName(this, tr("Open bank"), (dir.isEmpty() ? "./" : dir),
												"WOPN bank (*.wopn)");
	if (file.isNull()) return;

	std::unique_ptr<AbstractBank> bank;
	try {
		bank.reset(BankIO::loadBank(file.toLocal8Bit().toStdString()));
		config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
		return;
	}

	InstrumentSelectionDialog dlg(*bank, tr("Select instruments to load:"), this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	QVector<size_t> selection = dlg.currentInstrumentSelection();

	try {
		for (size_t index : selection) {
			int n = bt_->findFirstFreeInstrumentNumber();
			if (n == -1){
				QMessageBox::critical(this, tr("Error"), tr("Failed to load instrument."));
				return;
			}

			bt_->importInstrument(*bank, index, n);

			auto inst = bt_->getInstrument(n);
			auto name = inst->getName();
			comStack_->push(new AddInstrumentQtCommand(ui->instrumentListWidget, n,
													   QString::fromUtf8(name.c_str(), name.length()),
													   inst->getSoundSource(), instForms_));
		}
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
	}
}

/********** Undo-Redo **********/
void MainWindow::undo()
{
	bt_->undo();
	comStack_->undo();
}

void MainWindow::redo()
{
	bt_->redo();
	comStack_->redo();
}

/********** Load data **********/
void MainWindow::loadModule()
{
	instForms_->clearAll();
	ui->instrumentListWidget->clear();

	auto modTitle = bt_->getModuleTitle();
	ui->modTitleLineEdit->setText(QString::fromUtf8(modTitle.c_str(), modTitle.length()));
	auto modAuthor = bt_->getModuleAuthor();
	ui->authorLineEdit->setText(QString::fromUtf8(modAuthor.c_str(), modAuthor.length()));
	auto modCopyright = bt_->getModuleCopyright();
	ui->copyrightLineEdit->setText(QString::fromUtf8(modCopyright.c_str(), modCopyright.length()));
	ui->songNumSpinBox->setMaximum(bt_->getSongCount() - 1);
	highlight_->setValue(bt_->getModuleStepHighlightDistance());

	for (auto& idx : bt_->getInstrumentIndices()) {
		auto inst = bt_->getInstrument(idx);
		auto name = inst->getName();
		comStack_->push(new AddInstrumentQtCommand(
							ui->instrumentListWidget, idx, QString::fromUtf8(name.c_str(), name.length()),
							inst->getSoundSource(), instForms_));
	}
	bt_->setCurrentInstrument(-1);
	statusInst_->setText(tr("No instrument"));

	switch (bt_->getSongStyle(bt_->getCurrentSongNumber()).type) {
	case SongType::STD:		statusStyle_->setText(tr("Standard"));			break;
	case SongType::FMEX:	statusStyle_->setText(tr("FM3ch expanded"));	break;
	}

	statusPlayPos_->setText("00/00");

	isSavedModBefore_ = false;

	loadSong();

	// Clear records
	QApplication::clipboard()->clear();
	comStack_->clear();
	bt_->clearCommandHistory();
}

void MainWindow::loadSong()
{
	// Init position
	if (ui->songNumSpinBox->value() >= bt_->getSongCount())
		bt_->setCurrentSongNumber(bt_->getSongCount() - 1);
	else
		bt_->setCurrentSongNumber(bt_->getCurrentSongNumber());
	bt_->setCurrentOrderNumber(0);
	bt_->setCurrentTrack(0);
	bt_->setCurrentStepNumber(0);

	// Init ui
	ui->orderList->onSongLoaded();
	ui->patternEditor->onSongLoaded();

	int curSong = bt_->getCurrentSongNumber();
	ui->songNumSpinBox->setValue(curSong);
	auto title = bt_->getSongTitle(curSong);
	ui->songTitleLineEdit->setText(QString::fromUtf8(title.c_str(), title.length()));
	switch (bt_->getSongStyle(curSong).type) {
	case SongType::STD:		ui->songStyleLineEdit->setText(tr("Standard"));			break;
	case SongType::FMEX:	ui->songStyleLineEdit->setText(tr("FM3ch expanded"));	break;
	}
	ui->tickFreqSpinBox->setValue(bt_->getModuleTickFrequency());
	ui->tempoSpinBox->setValue(bt_->getSongTempo(curSong));
	ui->speedSpinBox->setValue(bt_->getSongSpeed(curSong));
	ui->patternSizeSpinBox->setValue(bt_->getDefaultPatternSize(curSong));
	ui->grooveSpinBox->setValue(bt_->getSongGroove(curSong));
	ui->grooveSpinBox->setMaximum(bt_->getGrooveCount() - 1);
	if (bt_->isUsedTempoInSong(curSong)) {
		ui->tickFreqSpinBox->setEnabled(true);
		ui->tempoSpinBox->setEnabled(true);
		ui->speedSpinBox->setEnabled(true);
		ui->grooveCheckBox->setChecked(false);
		ui->grooveSpinBox->setEnabled(false);
	}
	else {

		ui->tickFreqSpinBox->setEnabled(false);
		ui->tempoSpinBox->setEnabled(false);
		ui->speedSpinBox->setEnabled(false);
		ui->grooveCheckBox->setChecked(true);
		ui->grooveSpinBox->setEnabled(true);
	}

	setWindowTitle();
}

/********** Play song **********/
void MainWindow::startPlaySong()
{
	bt_->startPlaySong();
	ui->patternEditor->updatePosition();
	lockControls(true);
}

void MainWindow::startPlayFromStart()
{
	bt_->startPlayFromStart();
	ui->patternEditor->updatePosition();
	lockControls(true);
}

void MainWindow::startPlayPattern()
{
	bt_->startPlayPattern();
	ui->patternEditor->updatePosition();
	lockControls(true);
}

void MainWindow::startPlayFromCurrentStep()
{
	bt_->startPlayFromCurrentStep();
	lockControls(true);
}

void MainWindow::stopPlaySong()
{
	bt_->stopPlaySong();
	lockControls(false);
	ui->patternEditor->update();
	ui->orderList->update();
}

void MainWindow::lockControls(bool isLock)
{
	ui->modSetDialogOpenToolButton->setEnabled(!isLock);
	ui->songNumSpinBox->setEnabled(!isLock);
}

/********** Octave change **********/
void MainWindow::changeOctave(bool upFlag)
{
	if (upFlag) octave_->stepUp();
	else octave_->stepDown();

	statusOctave_->setText(tr("Octave: %1").arg(bt_->getCurrentOctave()));
}

/********** Configuration change **********/
void MainWindow::changeConfiguration()
{
	if (config_->getUseSCCI()) {
		stream_->stop();
		if (!timer_) {
			/*
			timer_ = std::make_unique<Timer>();
			timer_->setInterval(1000 / bt_->getModuleTickFrequency());
			timer_->setFunction([&]{ onNewTickSignaled(); });
			*/
			timer_ = std::make_unique<QTimer>(this);
			timer_->setTimerType(Qt::PreciseTimer);
			timer_->setInterval(1000 / bt_->getModuleTickFrequency());
			timer_->setSingleShot(false);
			QObject::connect(timer_.get(), &QTimer::timeout, this, &MainWindow::onNewTickSignaled);

			if (scciDll_->isLoaded()) {
				SCCIFUNC getSoundInterfaceManager = reinterpret_cast<SCCIFUNC>(
														scciDll_->resolve("getSoundInterfaceManager"));
				bt_->useSCCI(getSoundInterfaceManager ? getSoundInterfaceManager() : nullptr);
			}
			else {
				bt_->useSCCI(nullptr);
			}

			timer_->start();
		}
	}
	else {
		timer_.reset();
		bt_->useSCCI(nullptr);
		stream_->setRate(config_->getSampleRate());
		stream_->setDuration(config_->getBufferLength());
		stream_->setDevice(
					QString::fromUtf8(config_->getSoundDevice().c_str(), config_->getSoundDevice().length()));
		stream_->start();
	}
	bt_->changeConfiguration(config_);

	update();
}

/******************************/
void MainWindow::setWindowTitle()
{
	int n = bt_->getCurrentSongNumber();
	auto filePathStd = bt_->getModulePath();
	auto songTitleStd = bt_->getSongTitle(n);
	QString filePath = QString::fromLocal8Bit(filePathStd.c_str(), filePathStd.length());
	QString fileName = filePath.isEmpty() ? tr("Untitled") : QFileInfo(filePath).fileName();
	QString songTitle = QString::fromUtf8(songTitleStd.c_str(), songTitleStd.length());
	if (songTitle.isEmpty()) songTitle = tr("Untitled");
	QMainWindow::setWindowTitle(QString("%1[*] [#%2 %3] - BambooTracker")
								.arg(fileName).arg(QString::number(n)).arg(songTitle));
}

void MainWindow::setModifiedTrue()
{
	isModifiedForNotCommand_ = true;
	setWindowModified(true);
}

/******************************/
/********** Instrument list events **********/
void MainWindow::on_instrumentListWidget_customContextMenuRequested(const QPoint &pos)
{
	auto& list = ui->instrumentListWidget;
	QPoint globalPos = list->mapToGlobal(pos);
	QMenu menu;

	// Leave Before Qt5.7.0 style due to windows xp
	QAction* add = menu.addAction(tr("&Add"));
	QObject::connect(add, &QAction::triggered, this, &MainWindow::addInstrument);
	QAction* remove = menu.addAction(tr("&Remove"));
	QObject::connect(remove, &QAction::triggered, this, [&]() {
		removeInstrument(ui->instrumentListWidget->currentRow());
	});
	menu.addSeparator();
	QAction* name = menu.addAction(tr("Edit &name"));
	QObject::connect(name, &QAction::triggered, this, &MainWindow::editInstrumentName);
	menu.addSeparator();
	QAction* clone = menu.addAction(tr("&Clone"));
	QObject::connect(clone, &QAction::triggered, this, &MainWindow::cloneInstrument);
	QAction* dClone = menu.addAction(tr("&Deep clone"));
	QObject::connect(dClone, &QAction::triggered, this, &MainWindow::deepCloneInstrument);
	menu.addSeparator();
	QAction* ldFile = menu.addAction(tr("&Load from file..."));
	QObject::connect(ldFile, &QAction::triggered, this, &MainWindow::loadInstrument);
	QAction* svFile = menu.addAction(tr("&Save to file..."));
	QObject::connect(svFile, &QAction::triggered, this, &MainWindow::saveInstrument);
	menu.addSeparator();
	QAction* edit = menu.addAction(tr("&Edit..."));
	QObject::connect(edit, &QAction::triggered, this, &MainWindow::editInstrument);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
	edit->setShortcutVisibleInContextMenu(true);
#endif
	edit->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_I));

	if (bt_->findFirstFreeInstrumentNumber() == -1) {    // Max size
		add->setEnabled(false);
		ldFile->setEnabled(false);
	}
	else {
		switch (bt_->getCurrentTrackAttribute().source) {
		case SoundSource::DRUM:	add->setEnabled(false);	break;
		default:	break;
		}
	}
	auto item = list->currentItem();
	if (item == nullptr) {    // Not selected
		remove->setEnabled(false);
		name->setEnabled(false);
		svFile->setEnabled(false);
		edit->setEnabled(false);
	}
	if (item == nullptr || bt_->findFirstFreeInstrumentNumber() == -1) {
		clone->setEnabled(false);
		dClone->setEnabled(false);
	}

	menu.exec(globalPos);
}

void MainWindow::on_instrumentListWidget_itemDoubleClicked(QListWidgetItem *item)
{
	Q_UNUSED(item)
	editInstrument();
}

void MainWindow::onInstrumentListWidgetItemAdded(const QModelIndex &parent, int start, int end)
{
	Q_UNUSED(parent)
	Q_UNUSED(end)

	// Set core data to editor when add insrument
	int n = ui->instrumentListWidget->item(start)->data(Qt::UserRole).toInt();
	auto& form = instForms_->getForm(n);
	auto playFunc = [&](int stat) {
		switch (stat) {
		case -1:	stopPlaySong();				break;
		case 0:		startPlaySong();			break;
		case 1:		startPlayFromStart();		break;
		case 2:		startPlayPattern();			break;
		case 3:		startPlayFromCurrentStep();	break;
		default:	break;
		}
	};
	switch (instForms_->getFormInstrumentSoundSource(n)) {
	case SoundSource::FM:
	{
		auto fmForm = qobject_cast<InstrumentEditorFMForm*>(form.get());
		fmForm->setCore(bt_);
		fmForm->setConfiguration(config_);
		fmForm->setColorPalette(palette_);
		fmForm->resize(config_->getInstrumentFMWindowWidth(), config_->getInstrumentFMWindowHeight());

		QObject::connect(fmForm, &InstrumentEditorFMForm::envelopeNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMEnvelopeNumberChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::envelopeParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMEnvelopeParameterChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::lfoNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMLFONumberChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::lfoParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMLFOParameterChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::operatorSequenceNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMOperatorSequenceNumberChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::operatorSequenceParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMOperatorSequenceParameterChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::arpeggioNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMArpeggioNumberChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::arpeggioParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMArpeggioParameterChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::pitchNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMPitchNumberChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::pitchParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentFMPitchParameterChanged);
		QObject::connect(fmForm, &InstrumentEditorFMForm::jamKeyOnEvent,
						 this, &MainWindow::keyPressEvent, Qt::DirectConnection);
		QObject::connect(fmForm, &InstrumentEditorFMForm::jamKeyOffEvent,
						 this, &MainWindow::keyReleaseEvent, Qt::DirectConnection);
		QObject::connect(fmForm, &InstrumentEditorFMForm::octaveChanged,
						 this, &MainWindow::changeOctave, Qt::DirectConnection);
		QObject::connect(fmForm, &InstrumentEditorFMForm::modified,
						 this, &MainWindow::setModifiedTrue);
		QObject::connect(fmForm, &InstrumentEditorFMForm::playStatusChanged, this, playFunc);

		fmForm->installEventFilter(this);

		instForms_->onInstrumentFMEnvelopeNumberChanged();
		instForms_->onInstrumentFMLFONumberChanged();
		instForms_->onInstrumentFMOperatorSequenceNumberChanged();
		instForms_->onInstrumentFMArpeggioNumberChanged();
		instForms_->onInstrumentFMPitchNumberChanged();
		break;
	}
	case SoundSource::SSG:
	{
		auto ssgForm = qobject_cast<InstrumentEditorSSGForm*>(form.get());
		ssgForm->setCore(bt_);
		ssgForm->setConfiguration(config_);
		ssgForm->setColorPalette(palette_);
		ssgForm->resize(config_->getInstrumentSSGWindowWidth(), config_->getInstrumentSSGWindowHeight());

		QObject::connect(ssgForm, &InstrumentEditorSSGForm::waveFormNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGWaveFormNumberChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::waveFormParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGWaveFormParameterChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::toneNoiseNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGToneNoiseNumberChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::toneNoiseParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGToneNoiseParameterChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::envelopeNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGEnvelopeNumberChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::envelopeParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGEnvelopeParameterChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::arpeggioNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGArpeggioNumberChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::arpeggioParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGArpeggioParameterChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::pitchNumberChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGPitchNumberChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::pitchParameterChanged,
						 instForms_.get(), &InstrumentFormManager::onInstrumentSSGPitchParameterChanged);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::jamKeyOnEvent,
						 this, &MainWindow::keyPressEvent, Qt::DirectConnection);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::jamKeyOffEvent,
						 this, &MainWindow::keyReleaseEvent, Qt::DirectConnection);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::octaveChanged,
						 this, &MainWindow::changeOctave, Qt::DirectConnection);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::modified,
						 this, &MainWindow::setModifiedTrue);
		QObject::connect(ssgForm, &InstrumentEditorSSGForm::playStatusChanged, this, playFunc);

		ssgForm->installEventFilter(this);

		instForms_->onInstrumentSSGWaveFormNumberChanged();
		instForms_->onInstrumentSSGToneNoiseNumberChanged();
		instForms_->onInstrumentSSGEnvelopeNumberChanged();
		instForms_->onInstrumentSSGArpeggioNumberChanged();
		instForms_->onInstrumentSSGPitchNumberChanged();
		break;
	}
	default:
		break;
	}
}

void MainWindow::on_instrumentListWidget_itemSelectionChanged()
{
	int num = (ui->instrumentListWidget->currentRow() == -1)
			  ? -1
			  : ui->instrumentListWidget->currentItem()->data(Qt::UserRole).toInt();
	bt_->setCurrentInstrument(num);

	if (num == -1) statusInst_->setText(tr("No instrument"));
	else statusInst_->setText(
				tr("Instrument: ") + QString("%1").arg(num, 2, 16, QChar('0')).toUpper());

	bool isEnabled = (num != -1);
	ui->actionRemove_Instrument->setEnabled(isEnabled);
	ui->actionClone_Instrument->setEnabled(isEnabled);
	ui->actionDeep_Clone_Instrument->setEnabled(isEnabled);
	ui->actionSave_To_File->setEnabled(isEnabled);
	ui->actionEdit->setEnabled(isEnabled);
}

void MainWindow::on_grooveCheckBox_stateChanged(int arg1)
{
	if (arg1 == Qt::Checked) {
		ui->tickFreqSpinBox->setValue(60);
		ui->tickFreqSpinBox->setEnabled(false);
		ui->tempoSpinBox->setValue(150);
		ui->tempoSpinBox->setEnabled(false);
		ui->speedSpinBox->setEnabled(false);
		ui->grooveSpinBox->setEnabled(true);
		bt_->toggleTempoOrGrooveInSong(bt_->getCurrentSongNumber(), false);
	}
	else {
		ui->tickFreqSpinBox->setEnabled(true);
		ui->tempoSpinBox->setEnabled(true);
		ui->speedSpinBox->setEnabled(true);
		ui->grooveSpinBox->setEnabled(false);
		bt_->toggleTempoOrGrooveInSong(bt_->getCurrentSongNumber(), true);
	}

	setModifiedTrue();
}

void MainWindow::on_actionExit_triggered()
{
	close();
}

void MainWindow::on_actionUndo_triggered()
{
	undo();
}

void MainWindow::on_actionRedo_triggered()
{
	redo();
}

void MainWindow::on_actionCut_triggered()
{
	if (isEditedPattern_) ui->patternEditor->cutSelectedCells();
}

void MainWindow::on_actionCopy_triggered()
{
	if (isEditedPattern_) ui->patternEditor->copySelectedCells();
	else if (isEditedOrder_) ui->orderList->copySelectedCells();
}

void MainWindow::on_actionPaste_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onPastePressed();
	else if (isEditedOrder_) ui->orderList->onPastePressed();
}

void MainWindow::on_actionDelete_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onDeletePressed();
	else if (isEditedOrder_) ui->orderList->deleteOrder();
}

void MainWindow::updateMenuByPattern()
{
	isEditedPattern_ = true;
	isEditedOrder_ = false;

	if (bt_->isJamMode()) {
		// Edit
		ui->actionPaste->setEnabled(false);
		ui->actionMix->setEnabled(false);
		ui->actionOverwrite->setEnabled(false);
		ui->actionDelete->setEnabled(false);
		// Pattern
		ui->actionInterpolate->setEnabled(false);
		ui->actionReverse->setEnabled(false);
		ui->actionReplace_Instrument->setEnabled(false);
		ui->actionExpand->setEnabled(false);
		ui->actionShrink->setEnabled(false);
		ui->actionDecrease_Note->setEnabled(false);
		ui->actionIncrease_Note->setEnabled(false);
		ui->actionDecrease_Octave->setEnabled(false);
		ui->actionIncrease_Octave->setEnabled(false);
	}
	else {
		// Edit
		bool enabled = QApplication::clipboard()->text().startsWith("PATTERN_");
		ui->actionPaste->setEnabled(enabled);
		ui->actionMix->setEnabled(enabled);
		ui->actionOverwrite->setEnabled(enabled);
		ui->actionDelete->setEnabled(true);
		// Pattern
		ui->actionInterpolate->setEnabled(isSelectedPO_);
		ui->actionReverse->setEnabled(isSelectedPO_);
		ui->actionReplace_Instrument->setEnabled(
					isSelectedPO_ && ui->instrumentListWidget->currentRow() != -1);
		ui->actionExpand->setEnabled(isSelectedPO_);
		ui->actionShrink->setEnabled(isSelectedPO_);
		ui->actionDecrease_Note->setEnabled(true);
		ui->actionIncrease_Note->setEnabled(true);
		ui->actionDecrease_Octave->setEnabled(true);
		ui->actionIncrease_Octave->setEnabled(true);
	}

	// Song
	ui->actionInsert_Order->setEnabled(false);
	ui->actionRemove_Order->setEnabled(false);
	ui->actionDuplicate_Order->setEnabled(false);
	ui->actionMove_Order_Up->setEnabled(false);
	ui->actionMove_Order_Down->setEnabled(false);
	ui->actionClone_Patterns->setEnabled(false);
	ui->actionClone_Order->setEnabled(false);
}

void MainWindow::updateMenuByOrder()
{
	isEditedPattern_ = false;
	isEditedOrder_ = true;

	if (bt_->isJamMode()) {
		// Edit
		ui->actionPaste->setEnabled(false);
		ui->actionDelete->setEnabled(false);
		// Song
		ui->actionInsert_Order->setEnabled(false);
		ui->actionRemove_Order->setEnabled(false);
		ui->actionDuplicate_Order->setEnabled(false);
		ui->actionMove_Order_Up->setEnabled(false);
		ui->actionMove_Order_Down->setEnabled(false);
		ui->actionClone_Patterns->setEnabled(false);
		ui->actionClone_Order->setEnabled(false);
	}
	else {
		// Edit
		bool enabled = QApplication::clipboard()->text().startsWith("ORDER_");
		ui->actionPaste->setEnabled(enabled);
		ui->actionDelete->setEnabled(true);
		// Song
		bool canAdd = bt_->canAddNewOrder(bt_->getCurrentSongNumber());
		ui->actionInsert_Order->setEnabled(canAdd);
		ui->actionRemove_Order->setEnabled(true);
		ui->actionDuplicate_Order->setEnabled(canAdd);
		ui->actionMove_Order_Up->setEnabled(true);
		ui->actionMove_Order_Down->setEnabled(true);
		ui->actionClone_Patterns->setEnabled(canAdd);
		ui->actionClone_Order->setEnabled(canAdd);
	}
	// Edit
	ui->actionMix->setEnabled(false);
	ui->actionOverwrite->setEnabled(false);

	// Pattern
	ui->actionInterpolate->setEnabled(false);
	ui->actionReverse->setEnabled(false);
	ui->actionReplace_Instrument->setEnabled(false);
	ui->actionExpand->setEnabled(false);
	ui->actionShrink->setEnabled(false);
	ui->actionDecrease_Note->setEnabled(false);
	ui->actionIncrease_Note->setEnabled(false);
	ui->actionDecrease_Octave->setEnabled(false);
	ui->actionIncrease_Octave->setEnabled(false);
}

void MainWindow::onPatternAndOrderFocusLost()
{
	/*
	ui->actionCopy->setEnabled(false);
	ui->actionCut->setEnabled(false);
	ui->actionPaste->setEnabled(false);
	ui->actionMix->setEnabled(false);
	ui->actionOverwrite->setEnabled(false);
	ui->actionDelete->setEnabled(false);
	*/
}

void MainWindow::updateMenuByPatternAndOrderSelection(bool isSelected)
{
	isSelectedPO_ = isSelected;

	if (bt_->isJamMode()) {
		// Edit
		ui->actionCopy->setEnabled(false);
		ui->actionCut->setEnabled(false);
		// Pattern
		ui->actionInterpolate->setEnabled(false);
		ui->actionReverse->setEnabled(false);
		ui->actionReplace_Instrument->setEnabled(false);
		ui->actionExpand->setEnabled(false);
		ui->actionShrink->setEnabled(false);
	}
	else {
		// Edit
		ui->actionCopy->setEnabled(isSelected);
		ui->actionCut->setEnabled(isEditedPattern_ ? isSelected : false);
		// Pattern
		bool enabled = (isEditedPattern_ && isEditedPattern_) ? isSelected : false;
		ui->actionInterpolate->setEnabled(enabled);
		ui->actionReverse->setEnabled(enabled);
		ui->actionReplace_Instrument->setEnabled(
					enabled && ui->instrumentListWidget->currentRow() != -1);
		ui->actionExpand->setEnabled(enabled);
		ui->actionShrink->setEnabled(enabled);
	}
}

void MainWindow::on_actionAll_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onSelectPressed(1);
	else if (isEditedOrder_) ui->orderList->onSelectPressed(1);
}

void MainWindow::on_actionNone_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onSelectPressed(0);
	else if (isEditedOrder_) ui->orderList->onSelectPressed(0);
}

void MainWindow::on_actionDecrease_Note_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onTransposePressed(false, false);
}

void MainWindow::on_actionIncrease_Note_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onTransposePressed(false, true);
}

void MainWindow::on_actionDecrease_Octave_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onTransposePressed(true, false);
}

void MainWindow::on_actionIncrease_Octave_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onTransposePressed(true, true);
}

void MainWindow::on_actionInsert_Order_triggered()
{
	if (isEditedOrder_) ui->orderList->insertOrderBelow();
}

void MainWindow::on_actionRemove_Order_triggered()
{
	if (isEditedOrder_) ui->orderList->deleteOrder();
}

void MainWindow::on_actionModule_Properties_triggered()
{
	ModulePropertiesDialog dialog(bt_);
	if (dialog.exec() == QDialog::Accepted
			&& showUndoResetWarningDialog(tr("Do you want to change song properties?"))) {
		bt_->stopPlaySong();
		lockControls(false);
		dialog.onAccepted();
		loadModule();
		setModifiedTrue();
		setWindowTitle();
	}
}

void MainWindow::on_actionNew_Instrument_triggered()
{
	addInstrument();
}

void MainWindow::on_actionRemove_Instrument_triggered()
{
	removeInstrument(ui->instrumentListWidget->currentRow());
}

void MainWindow::on_actionClone_Instrument_triggered()
{
	cloneInstrument();
}

void MainWindow::on_actionDeep_Clone_Instrument_triggered()
{
	deepCloneInstrument();
}

void MainWindow::on_actionEdit_triggered()
{
	editInstrument();
}

void MainWindow::on_actionPlay_triggered()
{
	startPlaySong();
}

void MainWindow::on_actionPlay_Pattern_triggered()
{
	startPlayPattern();
}

void MainWindow::on_actionPlay_From_Start_triggered()
{
	startPlayFromStart();
}

void MainWindow::on_actionPlay_From_Cursor_triggered()
{
	startPlayFromCurrentStep();
}

void MainWindow::on_actionStop_triggered()
{
	stopPlaySong();
}

void MainWindow::on_actionEdit_Mode_triggered()
{
	bt_->toggleJamMode();
	ui->orderList->changeEditable();
	ui->patternEditor->changeEditable();

	if (isEditedOrder_) updateMenuByOrder();
	else if (isEditedPattern_) updateMenuByPattern();
	updateMenuByPatternAndOrderSelection(isSelectedPO_);

	if (bt_->isJamMode()) statusDetail_->setText(tr("Change to jam mode"));
	else statusDetail_->setText(tr("Change to edit mode"));
}

void MainWindow::on_actionToggle_Track_triggered()
{
	ui->patternEditor->onToggleTrackPressed();
}

void MainWindow::on_actionSolo_Track_triggered()
{
	ui->patternEditor->onSoloTrackPressed();
}

void MainWindow::on_actionKill_Sound_triggered()
{
	bt_->killSound();
}

void MainWindow::on_actionAbout_triggered()
{
	QMessageBox box(QMessageBox::NoIcon,
					tr("About"),
					QString("<h2>BambooTracker v")
					+ QString::fromStdString(Version::ofApplicationInString())
					+ QString("</h2>"
							  "<b>YM2608 (OPNA) Music Tracker<br>"
							  "Copyright (C) 2018, 2019 Rerrah</b><br>"
							  "<hr>"
							  "Libraries:<br>"
							  "- libOPNMIDI by (C) Vitaly Novichkov (MIT License part)<br>"
							  "- MAME (MAME License)<br>"
							  "- SCCI (SCCI License)<br>"
							  "- Silk icon set 1.3 by (C) Mark James (CC BY 2.5)<br>"
							  "- Qt (GPL v2+ or LGPL v3)<br>"
							  "- VGMPlay by (C) Valley Bell (GPL v2)<br>"
							  "<br>"
							  "Also see changelog which lists contributors."),
					QMessageBox::Ok,
					this);
	box.setIconPixmap(QIcon(":/icon/app_icon").pixmap(QSize(44, 44)));
	box.exec();
}

void MainWindow::on_actionFollow_Mode_triggered()
{
	bt_->setFollowPlay(ui->actionFollow_Mode->isChecked());
}

void MainWindow::on_actionGroove_Settings_triggered()
{
	std::vector<std::vector<int>> seqs;
	for (size_t i = 0; i < bt_->getGrooveCount(); ++i) {
		seqs.push_back(bt_->getGroove(i));
	}

	GrooveSettingsDialog diag;
	diag.setGrooveSquences(seqs);
	if (diag.exec() == QDialog::Accepted) {
		bt_->stopPlaySong();
		lockControls(false);
		bt_->setGrooves(diag.getGrooveSequences());
		ui->grooveSpinBox->setMaximum(bt_->getGrooveCount() - 1);
		setModifiedTrue();
	}
}

void MainWindow::on_actionConfiguration_triggered()
{
	ConfigurationDialog diag(config_);
	QObject::connect(&diag, &ConfigurationDialog::applyPressed, this, &MainWindow::changeConfiguration);

	if (diag.exec() == QDialog::Accepted) {
		bt_->stopPlaySong();
		changeConfiguration();
		ConfigurationHandler::saveConfiguration(config_);
		lockControls(false);
	}
}

void MainWindow::on_actionExpand_triggered()
{
	ui->patternEditor->onExpandPressed();
}

void MainWindow::on_actionShrink_triggered()
{
	ui->patternEditor->onShrinkPressed();
}

void MainWindow::on_actionDuplicate_Order_triggered()
{
	ui->orderList->onDuplicatePressed();
}

void MainWindow::on_actionMove_Order_Up_triggered()
{
	ui->orderList->onMoveOrderPressed(true);
}

void MainWindow::on_actionMove_Order_Down_triggered()
{
	ui->orderList->onMoveOrderPressed(false);
}

void MainWindow::on_actionClone_Patterns_triggered()
{
	ui->orderList->onClonePatternsPressed();
}

void MainWindow::on_actionClone_Order_triggered()
{
	ui->orderList->onCloneOrderPressed();
}

void MainWindow::on_actionNew_triggered()
{
	if (isWindowModified()) {
		auto modTitleStd = bt_->getModuleTitle();
		QString modTitle = QString::fromUtf8(modTitleStd.c_str(), modTitleStd.length());
		if (modTitle.isEmpty()) modTitle = tr("Untitled");
		QMessageBox dialog(QMessageBox::Warning,
						   "BambooTracker",
						   tr("Save changes to %1?").arg(modTitle),
						   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
		switch (dialog.exec()) {
		case QMessageBox::Yes:
			if (!on_actionSave_triggered()) return;
			break;
		case QMessageBox::No:
			break;
		case QMessageBox::Cancel:
			return;
		default:
			break;
		}
	}

	bt_->stopPlaySong();
	lockControls(false);
	bt_->makeNewModule();
	loadModule();
	isModifiedForNotCommand_ = false;
	setWindowModified(false);
}

void MainWindow::on_actionComments_triggered()
{
	auto comment = bt_->getModuleComment();
	CommentEditDialog diag(QString::fromUtf8(comment.c_str(), comment.length()));
	if (diag.exec() == QDialog::Accepted) {
		bt_->setModuleComment(diag.getComment().toUtf8().toStdString());
		setModifiedTrue();
	}
}

bool MainWindow::on_actionSave_triggered()
{
	auto path = QString::fromLocal8Bit(bt_->getModulePath().c_str(), bt_->getModulePath().length());
	if (!path.isEmpty() && QFileInfo::exists(path) && QFileInfo(path).isFile()) {
		if (!isSavedModBefore_ && config_->getBackupModules()) {
			try {
				bt_->backupModule(path.toLocal8Bit().toStdString());
			}
			catch (...) {
				QMessageBox::critical(this, tr("Error"), tr("Failed to backup module."));
				return false;
			}
		}

		try {
			bt_->saveModule(bt_->getModulePath());
			isModifiedForNotCommand_ = false;
			isSavedModBefore_ = true;
			setWindowModified(false);
			setWindowTitle();
			return true;
		}
		catch (std::exception& e) {
			QMessageBox::critical(this, tr("Error"), e.what());
			return false;
		}
	}
	else {
		return on_actionSave_As_triggered();
	}
}

bool MainWindow::on_actionSave_As_triggered()
{
	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getSaveFileName(this, tr("Save module"), (dir.isEmpty() ? "./" : dir),
												"BambooTracker module file (*.btm)");
	if (file.isNull()) return false;
	if (!file.endsWith(".btm")) file += ".btm";	// For linux

	if (std::ifstream(file.toStdString()).is_open()) {	// Already exists
		if (!isSavedModBefore_ && config_->getBackupModules()) {
			try {
				bt_->backupModule(file.toLocal8Bit().toStdString());
			}
			catch (...) {
				QMessageBox::critical(this, tr("Error"), tr("Failed to backup module."));
				return false;
			}
		}
	}

	bt_->setModulePath(file.toLocal8Bit().toStdString());
	try {
		bt_->saveModule(bt_->getModulePath());
		isModifiedForNotCommand_ = false;
		isSavedModBefore_ = true;
		setWindowModified(false);
		setWindowTitle();
		config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
		return true;
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
		return false;
	}
}

void MainWindow::on_actionOpen_triggered()
{
	if (isWindowModified()) {
		auto modTitleStd = bt_->getModuleTitle();
		QString modTitle = QString::fromUtf8(modTitleStd.c_str(), modTitleStd.length());
		if (modTitle.isEmpty()) modTitle = tr("Untitled");
		QMessageBox dialog(QMessageBox::Warning,
						   "BambooTracker",
						   tr("Save changes to %1?").arg(modTitle),
						   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
		switch (dialog.exec()) {
		case QMessageBox::Yes:
			if (!on_actionSave_triggered()) return;
			break;
		case QMessageBox::No:
			break;
		case QMessageBox::Cancel:
			return;
		default:
			break;
		}
	}

	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getOpenFileName(this, tr("Open module"), (dir.isEmpty() ? "./" : dir),
												"BambooTracker module file (*.btm)");
	if (file.isNull()) return;

	bt_->stopPlaySong();
	lockControls(false);
	try {
		bt_->loadModule(file.toLocal8Bit().toStdString());
		loadModule();

		config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
		isModifiedForNotCommand_ = false;
		setWindowModified(false);
	}
	catch (std::exception& e) {
		QMessageBox::critical(this, tr("Error"), e.what());
	}
}

void MainWindow::on_actionLoad_From_File_triggered()
{
	loadInstrument();
}

void MainWindow::on_actionSave_To_File_triggered()
{
	saveInstrument();
}

void MainWindow::on_actionImport_From_Bank_File_triggered()
{
	importInstrumentsFromBank();
}

void MainWindow::on_actionInterpolate_triggered()
{
	ui->patternEditor->onInterpolatePressed();
}

void MainWindow::on_actionReverse_triggered()
{
	ui->patternEditor->onReversePressed();
}

void MainWindow::on_actionReplace_Instrument_triggered()
{
	ui->patternEditor->onReplaceInstrumentPressed();
}

void MainWindow::on_actionRow_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onSelectPressed(2);
	else if (isEditedOrder_) ui->orderList->onSelectPressed(2);
}

void MainWindow::on_actionColumn_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onSelectPressed(3);
	else if (isEditedOrder_) ui->orderList->onSelectPressed(3);
}

void MainWindow::on_actionPattern_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onSelectPressed(4);
	else if (isEditedOrder_) ui->orderList->onSelectPressed(4);
}

void MainWindow::on_actionOrder_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onSelectPressed(5);
	else if (isEditedOrder_) ui->orderList->onSelectPressed(5);
}

void MainWindow::on_actionRemove_Unused_Instruments_triggered()
{
	if (showUndoResetWarningDialog(tr("Do you want to remove all unused instruments?")))
	{
		bt_->stopPlaySong();
		lockControls(false);

		auto list = ui->instrumentListWidget;
		for (auto& n : bt_->getUnusedInstrumentIndices()) {
			for (int i = 0; i < list->count(); ++i) {
				if (list->item(i)->data(Qt::UserRole).toInt() == n) {
					removeInstrument(i);
				}
			}
		}
		bt_->clearUnusedInstrumentProperties();
		bt_->clearCommandHistory();
		comStack_->clear();
		setModifiedTrue();
	}
}

void MainWindow::on_actionRemove_Unused_Patterns_triggered()
{
	if (showUndoResetWarningDialog(tr("Do you want to remove all unused patterns?")))
	{
		bt_->stopPlaySong();
		lockControls(false);

		bt_->clearUnusedPatterns();
		bt_->clearCommandHistory();
		comStack_->clear();
		setModifiedTrue();
	}
}

void MainWindow::on_actionWAV_triggered()
{
	WaveExportSettingsDialog diag;
	if (diag.exec() != QDialog::Accepted) return;

	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getSaveFileName(this, tr("Export to wav"), (dir.isEmpty() ? "./" : dir),
												"WAV signed 16-bit PCM (*.wav)");
	if (file.isNull()) return;
	if (!file.endsWith(".wav")) file += ".wav";	// For linux

	QProgressDialog progress(
				tr("Export to WAV"),
				tr("Cancel"),
				0,
				bt_->getAllStepCount(bt_->getCurrentSongNumber(), diag.getLoopCount()) + 3
				);
	progress.setValue(0);
	progress.setWindowFlags(progress.windowFlags()
							& ~Qt::WindowContextHelpButtonHint
							& ~Qt::WindowCloseButtonHint);
	progress.show();

	bt_->stopPlaySong();
	lockControls(false);
	stream_->stop();

	try {
		bool res = bt_->exportToWav(file.toStdString(), diag.getLoopCount(),
									[&progress]() -> bool {
										QApplication::processEvents();
										progress.setValue(progress.value() + 1);
										return progress.wasCanceled();
									});
		if (res) config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
	}
	catch (...) {
		QMessageBox::critical(this, tr("Error"), tr("Failed to export to wav file."));
	}

	stream_->start();
}

void MainWindow::on_actionVGM_triggered()
{
	VgmExportSettingsDialog diag;
	if (diag.exec() != QDialog::Accepted) return;
	GD3Tag tag = diag.getGD3Tag();

	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getSaveFileName(this, tr("Export to vgm"), (dir.isEmpty() ? "./" : dir),
												"VGM file (*.vgm)");
	if (file.isNull()) return;
	if (!file.endsWith(".vgm")) file += ".vgm";	// For linux

	QProgressDialog progress(
				tr("Export to VGM"),
				tr("Cancel"),
				0,
				bt_->getAllStepCount(bt_->getCurrentSongNumber(), 1) + 3
				);
	progress.setValue(0);
	progress.setWindowFlags(progress.windowFlags()
							& ~Qt::WindowContextHelpButtonHint
							& ~Qt::WindowCloseButtonHint);
	progress.show();

	bt_->stopPlaySong();
	lockControls(false);
	stream_->stop();

	try {
		bool res = bt_->exportToVgm(file.toStdString(),
									diag.enabledGD3(),
									tag,
									[&progress]() -> bool {
										QApplication::processEvents();
										progress.setValue(progress.value() + 1);
										return progress.wasCanceled();
									});
		if (res) config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
	}
	catch (...) {
		QMessageBox::critical(this, tr("Error"), tr("Failed to export to vgm file."));
	}

	stream_->start();
}

void MainWindow::on_actionS98_triggered()
{
	S98ExportSettingsDialog diag;
	if (diag.exec() != QDialog::Accepted) return;
	S98Tag tag = diag.getS98Tag();

	QString dir = QString::fromStdString(config_->getWorkingDirectory());
	QString file = QFileDialog::getSaveFileName(this, tr("Export to s98"), (dir.isEmpty() ? "./" : dir),
												"S98 file (*.s98)");
	if (file.isNull()) return;
	if (!file.endsWith(".s98")) file += ".s98";	// For linux

	QProgressDialog progress(
				tr("Export to S98"),
				tr("Cancel"),
				0,
				bt_->getAllStepCount(bt_->getCurrentSongNumber(), 1) + 3
				);
	progress.setValue(0);
	progress.setWindowFlags(progress.windowFlags()
							& ~Qt::WindowContextHelpButtonHint
							& ~Qt::WindowCloseButtonHint);
	progress.show();

	bt_->stopPlaySong();
	lockControls(false);
	stream_->stop();

	try {
		bool res = bt_->exportToS98(file.toStdString(),
									diag.enabledTag(),
									tag,
									[&progress]() -> bool {
										QApplication::processEvents();
										progress.setValue(progress.value() + 1);
										return progress.wasCanceled();
									});
		if (res) config_->setWorkingDirectory(QFileInfo(file).dir().path().toStdString());
	}
	catch (...) {
		QMessageBox::critical(this, tr("Error"), tr("Failed to export to s98 file."));
	}

	stream_->start();
}

void MainWindow::on_actionMix_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onPasteMixPressed();
}

void MainWindow::on_actionOverwrite_triggered()
{
	if (isEditedPattern_) ui->patternEditor->onPasteOverwritePressed();
}

void MainWindow::onNewTickSignaled()
{
	if (!bt_->streamCountUp()) {	// New step
		ui->orderList->update();
		ui->patternEditor->updatePosition();
		statusPlayPos_->setText(
					QString("%1/%2").arg(bt_->getPlayingOrderNumber(), 2, 16, QChar('0'))
					.arg(bt_->getPlayingStepNumber(), 2, 16, QChar('0')).toUpper());
	}
}
