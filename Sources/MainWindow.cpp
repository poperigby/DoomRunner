//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Created on:  13.5.2019
// Description:
//======================================================================================================================

#include "MainWindow.hpp"
#include "ui_MainWindow.h"

#include "SetupDialog.hpp"
#include "DMFlagsDialog.hpp"
#include "CompatFlagsDialog.hpp"
#include "JsonHelper.hpp"
#include "Utils.hpp"

#include <QString>
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QTimer>
#include <QProcess>
#include <QDesktopWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include <QDebug>


//======================================================================================================================

#ifdef _WIN32
	static const QString scriptFileExt = "*.bat";
#else
	static const QString scriptFileExt = "*.sh";
#endif

static constexpr char defaultOptionsFile [] = "options.json";


//======================================================================================================================
//  fixed combo boxes values

enum Skill {
	TOO_YOUNG_TO_DIE,
	NOT_TOO_ROUGH,
	HURT_ME_PLENTY,
	ULTRA_VIOLENCE,
	NIGHTMARE,
	CUSTOM
};

enum MultRole {
	SERVER,
	CLIENT
};

enum NetMode {
	PEER_TO_PEER,
	PACKET_SERVER
};

enum GameMode {
	DEATHMATCH,
	TEAM_DEATHMATCH,
	ALT_DEATHMATCH,
	ALT_TEAM_DEATHMATCH,
	COOPERATIVE
};


//======================================================================================================================
//  MainWindow

MainWindow::MainWindow()

	: QMainWindow( nullptr )
	, shown( false )
	, width( -1 )
	, height( -1 )
	, tickCount( 0 )
	, pathHelper( false )
	, engineModel( engines, []( const Engine & engine ) { return engine.name; } )
	, iwadModel( iwads, []( const IWAD & iwad ) { return iwad.name; } )
	, iwadListFromDir( false )
	, mapModel( maps, []( const MapPack & pack ) { return pack.name; } )
	, selectedPackIdx( -1 )
	, modModel( mods, []( Mod & mod ) -> QString & { return mod.name; } )
	, presetModel( presets, []( Preset & preset ) -> QString & { return preset.name; } )
	, dmflags1( 0 )
	, dmflags2( 0 )
	, compatflags1( 0 )
	, compatflags2( 0 )
{
	ui = new Ui::MainWindow;
	ui->setupUi( this );

	// setup view models
	ui->engineCmbBox->setModel( &engineModel );
	ui->presetListView->setModel( &presetModel );
	ui->iwadListView->setModel( &iwadModel );
	ui->mapListView->setModel( &mapModel );
	ui->modListView->setModel( &modModel );

	// setup preset list view
	ui->presetListView->toggleIntraWidgetDragAndDrop( true );
	ui->presetListView->toggleInterWidgetDragAndDrop( false );
	ui->presetListView->toggleExternalFileDragAndDrop( false );
	connect( ui->presetListView, &EditableListView::itemsDropped, this, &thisClass::presetDropped );
	ui->presetListView->toggleNameEditing( true );

	// setup mod list view
	ui->modListView->toggleIntraWidgetDragAndDrop( true );
	ui->modListView->toggleInterWidgetDragAndDrop( false );
	ui->modListView->toggleExternalFileDragAndDrop( true );
	connect( ui->modListView, &EditableListView::itemsDropped, this, &thisClass::modsDropped );
	ui->modListView->toggleNameEditing( false );
	modModel.setAssignFileFunc(  // this will be our reaction when a file is dragged and dropped from a directory window
		[ this ]( Mod & mod, const QFileInfo & file ) {
			mod.name = file.fileName();
			mod.path = pathHelper.convertPath( file.filePath() );
		}
	);
	modModel.setIsCheckedFunc(  // here the model will read and write the information about check state
		[]( Mod & mod ) -> bool & {
			return mod.checked;
		}
	);
	modModel.toggleCheckable( true );  // allow user to check/uncheck each item in the list

	// setup signals
	connect( ui->setupPathsAction, &QAction::triggered, this, &thisClass::runSetupDialog );
	connect( ui->exportPresetAction, &QAction::triggered, this, &thisClass::exportPreset );
	connect( ui->importPresetAction, &QAction::triggered, this, &thisClass::importPreset );
	connect( ui->exitAction, &QAction::triggered, this, &thisClass::close );

	connect( ui->engineCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectEngine );
	connect( ui->configCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectConfig );
	connect( ui->presetListView, &QListView::clicked, this, &thisClass::selectPreset );
	connect( ui->iwadListView, &QListView::clicked, this, &thisClass::toggleIWAD );
	connect( ui->mapListView, &QListView::clicked, this, &thisClass::toggleMapPack );
	connect( ui->modListView, &QListView::clicked, this, &thisClass::toggleMod );

	connect( ui->presetBtnAdd, &QToolButton::clicked, this, &thisClass::presetAdd );
	connect( ui->presetBtnDel, &QToolButton::clicked, this, &thisClass::presetDelete );
	connect( ui->presetBtnClone, &QToolButton::clicked, this, &thisClass::presetClone );
	connect( ui->presetBtnUp, &QToolButton::clicked, this, &thisClass::presetMoveUp );
	connect( ui->presetBtnDown, &QToolButton::clicked, this, &thisClass::presetMoveDown );

	connect( ui->modBtnAdd, &QToolButton::clicked, this, &thisClass::modAdd );
	connect( ui->modBtnDel, &QToolButton::clicked, this, &thisClass::modDelete );
	connect( ui->modBtnUp, &QToolButton::clicked, this, &thisClass::modMoveUp );
	connect( ui->modBtnDown, &QToolButton::clicked, this, &thisClass::modMoveDown );

	connect( ui->launchMode_menu, &QRadioButton::clicked, this, &thisClass::modeGameMenu );
	connect( ui->launchMode_map, &QRadioButton::clicked, this, &thisClass::modeSelectedMap );
	connect( ui->launchMode_savefile, &QRadioButton::clicked, this, &thisClass::modeSavedGame );
	connect( ui->mapCmbBox, &QComboBox::currentTextChanged, this, &thisClass::selectMap );
	connect( ui->saveFileCmbBox, &QComboBox::currentTextChanged, this, &thisClass::selectSavedGame );
	connect( ui->skillCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectSkill );
	connect( ui->skillSpinBox, QOverload<int>::of( &QSpinBox::valueChanged ), this, &thisClass::changeSkillNum );
	connect( ui->noMonstersChkBox, &QCheckBox::toggled, this, &thisClass::toggleNoMonsters );
	connect( ui->fastMonstersChkBox, &QCheckBox::toggled, this, &thisClass::toggleFastMonsters );
	connect( ui->monstersRespawnChkBox, &QCheckBox::toggled, this, &thisClass::toggleMonstersRespawn );
	connect( ui->dmFlagsBtn, &QPushButton::clicked, this, &thisClass::runDMFlagsDialog );
	connect( ui->compatFlagsBtn, &QPushButton::clicked, this, &thisClass::runCompatFlagsDialog );

	connect( ui->multiplayerChkBox, &QCheckBox::toggled, this, &thisClass::toggleMultiplayer );
	connect( ui->multRoleCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectMultRole );
	connect( ui->hostnameLine, &QLineEdit::textChanged, this, &thisClass::changeHost );
	connect( ui->portSpinBox, QOverload<int>::of( &QSpinBox::valueChanged ), this, &thisClass::changePort );
	connect( ui->netModeCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectNetMode );
	connect( ui->gameModeCmbBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &thisClass::selectGameMode );
	connect( ui->playerCountSpinBox, QOverload<int>::of( &QSpinBox::valueChanged ), this, &thisClass::changePlayerCount );
	connect( ui->teamDmgSpinBox, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, &thisClass::changeTeamDamage );
	connect( ui->timeLimitSpinBox, QOverload<int>::of( &QSpinBox::valueChanged ), this, &thisClass::changeTimeLimit );

	connect( ui->cmdArgsLine, &QLineEdit::textChanged, this, &thisClass::updateAdditionalArgs );
	connect( ui->launchBtn, &QPushButton::clicked, this, &thisClass::launch );

	// try to load last saved state
	if (QFileInfo( defaultOptionsFile ).exists())
		loadOptions( defaultOptionsFile );
	else  // this is a first run, perform an initial setup
		QTimer::singleShot( 1, this, &thisClass::runSetupDialog );

	// setup an update timer
	startTimer( 1000 );
}

void MainWindow::showEvent( QShowEvent * )
{
	// the window doesn't have the geometry.x and geometry.y set yet in the constructor, so we must do it here
	if (width > 0 && height > 0) {
		const QRect & geometry = this->geometry();
		this->setGeometry( geometry.x(), geometry.y(), width, height );
	}

	shown = true;
}

void MainWindow::timerEvent( QTimerEvent * )  // called once per second
{
	tickCount++;

	if (tickCount % 2 == 0) {
		updateListsFromDirs();
	}

	if (tickCount % 60 == 0) {
		saveOptions( defaultOptionsFile );
	}
}

void MainWindow::closeEvent( QCloseEvent * event )
{
	saveOptions( defaultOptionsFile );

	QWidget::closeEvent( event );
}

MainWindow::~MainWindow()
{
	delete ui;
}


//----------------------------------------------------------------------------------------------------------------------
//  dialogs

void MainWindow::runSetupDialog()
{
	SetupDialog dialog( this, pathHelper, engines, iwads, iwadListFromDir, iwadDir, mapDir, modDir );
	connect( &dialog, &SetupDialog::iwadListNeedsUpdate, this, &thisClass::updateIWADsFromDir );
	connect( &dialog, &SetupDialog::absolutePathsToggled, this, &thisClass::toggleAbsolutePaths );
	dialog.exec();

	// update the views in this window, because the dialog may have changed the underlying data
	engineModel.updateView( 0 );
	iwadModel.updateView( 0 );

	generateLaunchCommand();
}

void MainWindow::runDMFlagsDialog()
{
	DMFlagsDialog dialog( this, dmflags1, dmflags2 );
	dialog.exec();

	generateLaunchCommand();
}

void MainWindow::runCompatFlagsDialog()
{
	QMessageBox::warning( this, "Not implemented", "Sorry, this feature is not implemented yet" );

	//CompatFlagsDialog dialog( this, compatflags1, compatflags2 );
	//dialog.exec();

	//generateLaunchCommand();
}


//----------------------------------------------------------------------------------------------------------------------
//  list item selection

void MainWindow::selectPreset( const QModelIndex & index )
{
	Preset & preset = presets[ index.row() ];

	// restore selected engine
	ui->engineCmbBox->setCurrentIndex( -1 );  // deselect current engine
	if (!preset.selectedEnginePath.isEmpty()) {  // the engine combo box might have been empty when creating this preset
		int engineIdx = findSuch<Engine>( engines, [ &preset ]( const Engine & engine )
		                                           { return engine.path == preset.selectedEnginePath; } );
		if (engineIdx >= 0) {
			ui->engineCmbBox->setCurrentIndex( engineIdx );
		} else {
			QMessageBox::warning( this, "Engine no longer exists",
				"Engine selected for this preset ("%preset.selectedEnginePath%") no longer exists, please select another one." );
		}
	}

	// restore selected config
	ui->configCmbBox->setCurrentIndex( -1 );  // deselect current config
	if (!preset.selectedConfig.isEmpty()) {  // the preset combo box might have been empty when creating this preset
		int configIdx = ui->configCmbBox->findText( preset.selectedConfig );
		if (configIdx >= 0) {
			ui->configCmbBox->setCurrentIndex( configIdx );
		} else {
			QMessageBox::warning( this, "Config no longer exists",
				"Config selected for this preset ("%preset.selectedConfig%") no longer exists, please select another one." );
		}
	}

	// restore selected IWAD
	deselectSelectedItems( ui->iwadListView );
	selectedIWADIdx = -1;
	if (!preset.selectedIWAD.isEmpty()) {  // the IWAD may have not been selected when creating this preset
		int iwadIdx = findSuch<IWAD>( iwads, [ &preset ]( const IWAD & iwad ) { return iwad.name == preset.selectedIWAD; } );
		if (iwadIdx >= 0) {
			selectItemByIdx( ui->iwadListView, iwadIdx );
			selectedIWADIdx = iwadIdx;
			updateMapsFromIWAD();
		} else {
			QMessageBox::warning( this, "IWAD no longer exists",
				"IWAD selected for this preset ("%preset.selectedIWAD%") no longer exists, please select another one." );
		}
	}
/*
	// restore selected MapPack
	deselectSelectedItem( ui->mapListView );
	selectedPackIdx = -1;
	if (!preset.selectedMapPack.isEmpty()) {  // the map pack may have not been selected when creating this preset
		int mapIdx = findSuch<MapPack>( maps, [ &preset ]( const MapPack & pack ) { return pack.name == preset.selectedMapPack; } );
		if (mapIdx >= 0) {
			selectItemByIdx( ui->mapListView, mapIdx );
			selectedPackIdx = mapIdx;
		} else {
			QMessageBox::warning( this, "IWAD no longer exists",
				"Map pack selected for this preset ("%preset.selectedMapPack%") no longer exists, please select another one." );
		}
	}
*/
	// restore list of mods
	deselectSelectedItems( ui->modListView );
	mods.clear();
	for (auto modIt = preset.mods.begin(); modIt != preset.mods.end(); )  // need iterator, so that we can erase non-existing
	{
		const Mod & mod = *modIt;

		if (!QFileInfo( mod.path ).exists()) {
			QMessageBox::warning( this, "Mod no longer exists",
				"A mod from the preset ("%mod.path%") no longer exists. It will be removed from the list." );
			modIt = preset.mods.erase( modIt );  // keep the list widget in sync with the preset list
			continue;
		}

		mods.append( mod );
		modIt++;
	}
	modModel.updateView(0);

	generateLaunchCommand();
}

void MainWindow::selectEngine( int index )
{
	if (index < 0) {  // engine combo box was reset to "no engine selected" state
		return;
	}

	// update the current preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].selectedEnginePath = engines[ index ].path;
	}

	updateSaveFilesFromDir();
	updateConfigFilesFromDir();

	generateLaunchCommand();
}

void MainWindow::selectConfig( int index )
{
	if (index < 0) {  // config combo box was reset to "no config selected" state
		return;
	}

	// TODO: Adding an item to an empty combobox automatically causes selectConfig(0), which we don't want
	//       because we want to allow the config to be unselected. Find a way how to get around this.

	// update the current preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].selectedConfig = ui->configCmbBox->itemText( index );
	}

	updateSaveFilesFromDir();

	generateLaunchCommand();
}

void MainWindow::toggleIWAD( const QModelIndex & index )
{
	// allow the user to deselect the IWAD by clicking on it again
	int clickedIWADIdx = index.row();
	if (clickedIWADIdx == selectedIWADIdx) {
		selectedIWADIdx = -1;
		ui->iwadListView->selectionModel()->select( index, QItemSelectionModel::Deselect );
	} else {
		selectedIWADIdx = clickedIWADIdx;
	}

	// update the current preset
	int clickedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (clickedPresetIdx >= 0) {
		if (selectedIWADIdx >= 0)
			presets[ clickedPresetIdx ].selectedIWAD = iwads[ index.row() ].name;
		else
			presets[ clickedPresetIdx ].selectedIWAD.clear();
	}

	updateMapsFromIWAD();

	generateLaunchCommand();
}

void MainWindow::toggleMapPack( const QModelIndex & index )
{
	// allow the user to deselect the map pack by clicking on it again
	int clickedPackIdx = index.row();
	if (clickedPackIdx == selectedPackIdx) {
		selectedPackIdx = -1;
		ui->mapListView->selectionModel()->select( index, QItemSelectionModel::Deselect );
	} else {
		selectedPackIdx = clickedPackIdx;
	}
/*
	// update the current preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		if (selectedPackIdx >= 0)
			presets[ selectedPresetIdx ].selectedMapPack = maps[ clickedPackIdx ].name;
		else
			presets[ selectedPresetIdx ].selectedMapPack.clear();  // deselect it also from preset
	}
*/
	generateLaunchCommand();
}

void MainWindow::toggleMod( const QModelIndex & modIndex )
{
	// update the current preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].mods[ modIndex.row() ].checked = mods[ modIndex.row() ].checked;
	}

	generateLaunchCommand();
}


//----------------------------------------------------------------------------------------------------------------------
//  preset list manipulation

void MainWindow::presetAdd()
{
  static uint presetNum = 1;

	// clear the widgets displaying preset content
	ui->engineCmbBox->setCurrentIndex( -1 );
	ui->configCmbBox->setCurrentIndex( -1 );
	deselectSelectedItems( ui->iwadListView );
	selectedIWADIdx = -1;
	//deselectSelectedItem( ui->mapListView );
	//selectedPackIdx = -1;
	deselectSelectedItems( ui->modListView );
	mods.clear();
	modModel.updateView(0);

	// appends, changes selection and updates UI
	appendItem( ui->presetListView, presetModel, { "Preset"+QString::number( presetNum ), "", "", "", {} } );

	// open edit mode so that user can name the preset
	ui->presetListView->edit( presetModel.index( presets.count() - 1, 0 ) );

	presetNum++;
}

void MainWindow::presetDelete()
{
	deleteSelectedItem( ui->presetListView, presetModel );
}

void MainWindow::presetClone()
{
	int origIdx = cloneSelectedItem( ui->presetListView, presetModel );
	if (origIdx >= 0) {
		// open edit mode so that user can name the preset
		ui->presetListView->edit( presetModel.index( presets.count() - 1, 0 ) );
	}
}

void MainWindow::presetMoveUp()
{
	moveUpSelectedItem( ui->presetListView, presetModel );
}

void MainWindow::presetMoveDown()
{
	moveDownSelectedItem( ui->presetListView, presetModel );
}


//----------------------------------------------------------------------------------------------------------------------
//  mod list manipulation

void MainWindow::modAdd()
{
	QString path = QFileDialog::getOpenFileName( this, "Locate the mod file", modDir );
	if (path.isEmpty())  // user probably clicked cancel
		return;

	QString name = QFileInfo( path ).fileName();

	// the path comming out of the file dialog is always absolute
	if (pathHelper.useRelativePaths())
		path = pathHelper.getRelativePath( path );

	appendItem( ui->modListView, modModel, { name, path, true } );

	// add it also to the current preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].mods.append({ name, path, true });
	}

	generateLaunchCommand();
}

void MainWindow::modDelete()
{
	int deletedModIdx = deleteSelectedItem( ui->modListView, modModel );
	if (deletedModIdx < 0)  // no item was selected
		return;

	// remove it also from the current preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].mods.removeAt( deletedModIdx );
	}

	generateLaunchCommand();
}

void MainWindow::modMoveUp()
{
	int selectedModIdx = moveUpSelectedItem( ui->modListView, modModel );
	if (selectedModIdx <= 0)  // no item was selected or it was the topmost one
		return;

	// move it up also in the preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].mods.move( selectedModIdx, selectedModIdx - 1 );
	}

	generateLaunchCommand();
}

void MainWindow::modMoveDown()
{
	int selectedModIdx = moveDownSelectedItem( ui->modListView, modModel );
	if (selectedModIdx < 0 || selectedModIdx == mods.size() - 1)  // no item was selected or it was the lowest one
		return;

	// move it down also in the preset
	int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedPresetIdx >= 0) {
		presets[ selectedPresetIdx ].mods.move( selectedModIdx, selectedModIdx + 1 );
	}

	generateLaunchCommand();
}

// idiotic workaround because Qt is fucking retarded
//
// When an internal reordering drag&drop is performed, Qt doesn't update the selection and leaves the selection
// on the old indexes, where are now some completely different items.
// You can't manually update the indexes in the view, because at some point after dropMimeData Qt calls removeRows on items
// that are CURRENTLY SELECTED, instead of on items that were selected at the beginning of this drag&drop operation.
// So we must update the selection at some point AFTER the drag&drop operation is finished and the rows removed.
//
// The right place is in overriden method QAbstractItemView::startDrag.
// But outside an item model, there is no information abouth the target drop index. So the model must write down
// the index and then let other classes retrieve it at the right time.
//
// And like it wasn't enough, we can't retrieve the drop index in the view, because we cannot cast its abstract model
// into the correct model, because it's a template class whose template parameter is not known there.
// So the only way is to emit a signal to the owner of the ListView (MainWindow), which then catches it,
// queries the model for a drop index and then performs the update.
//
// Co-incidentally, this also solves the problem, that when items are dropped into a mod list, we need to update
// the current preset.
void MainWindow::presetDropped()
{
	if (presetModel.wasDroppedInto()) {
		// finaly update the selection
		int row = presetModel.droppedRow();
		deselectSelectedItems( ui->presetListView );
		selectItemByIdx( ui->presetListView, row );
	}
}

void MainWindow::modsDropped()
{
	if (modModel.wasDroppedInto())
	{
		// finaly update the selection
		int row = modModel.droppedRow();
		int count = modModel.droppedCount();
		deselectSelectedItems( ui->modListView );
		for (int i = 0; i < count; i++)
			selectItemByIdx( ui->modListView, row + i );

		// update the preset
		int selectedPresetIdx = getSelectedItemIdx( ui->presetListView );
		if (selectedPresetIdx >= 0) {
			presets[ selectedPresetIdx ].mods = mods; // not the most optimal way, but the size of the list is always low
		}

		generateLaunchCommand();
	}
}

//----------------------------------------------------------------------------------------------------------------------
//  automatic list updates according to directory content

void MainWindow::updateIWADsFromDir( QListView * view )  // the parameter exists, because SetupDialog also wants to update its view
{
	updateListFromDir< IWAD >( iwads, view, iwadDir, "wad",
		/*makeItemFromFile*/[ this ]( const QFileInfo & file ) -> IWAD
		{
			return { file.fileName(), pathHelper.convertPath( file.filePath() ) };
		}
	);

	// the selected item might have been changed during the update, so update our iternal mark
	selectedIWADIdx = getSelectedItemIdx( view );
}

void MainWindow::updateMapPacksFromDir()
{
	updateListFromDir< MapPack >( maps, ui->mapListView, mapDir, "wad",
		/*makeItemFromFile*/[]( const QFileInfo & file ) -> MapPack
		{
			return { file.fileName() };
		}
	);

	// the selected item might have been changed during the update, so update our iternal mark
	selectedPackIdx = getSelectedItemIdx( ui->mapListView );
}

void MainWindow::updateSaveFilesFromDir()
{
	if (ui->engineCmbBox->currentIndex() < 0) {  // no engine is selected
		return;
	}

	// write down the currently selected item
	QString curText = ui->saveFileCmbBox->currentText();
	ui->saveFileCmbBox->setCurrentIndex( -1 );  // just in case
	ui->saveFileCmbBox->clear();

	// update the list according to directory content
	QFileInfo info( engines[ ui->engineCmbBox->currentIndex() ].path );
	QDir dir = info.dir();
	QDirIterator dirIt( dir );
	while (dirIt.hasNext()) {
		QFileInfo entry( dirIt.next() );
		if (!entry.isDir() && entry.suffix() == "zds")
			ui->saveFileCmbBox->addItem( entry.fileName() );
	}

	// restore the originally selected item
	ui->saveFileCmbBox->setCurrentText( curText );
}

void MainWindow::updateConfigFilesFromDir()
{
	if (ui->engineCmbBox->currentIndex() < 0) {  // no engine is selected
		return;
	}

	// write down the currently selected item
	QString lastText = ui->configCmbBox->currentText();
	ui->configCmbBox->setCurrentIndex( -1 );  // just in case
	ui->configCmbBox->clear();

	// update the list according to directory content
	QFileInfo info( engines[ ui->engineCmbBox->currentIndex() ].path );
	QDir dir = info.dir();
	QDirIterator dirIt( dir );
	while (dirIt.hasNext()) {
		QFileInfo entry( dirIt.next() );
		if (!entry.isDir() && entry.suffix() == "ini")
			ui->configCmbBox->addItem( entry.fileName() );
	}

	// restore the originally selected item (reset selection if the item does not exist in the new content)
	int newIndex = ui->configCmbBox->findText( lastText );
	ui->configCmbBox->setCurrentIndex( newIndex );
}

void MainWindow::updateMapsFromIWAD()
{
	int iwadIdx = getSelectedItemIdx( ui->iwadListView );
	if (iwadIdx < 0)
		return;

	QString text = iwads[ iwadIdx ].name;

	if ((text.compare( "doom.wad", Qt::CaseInsensitive ) == 0 || text.startsWith( "doom1" , Qt::CaseInsensitive ))
	&& !ui->mapCmbBox->itemText(0).startsWith('E')) {
		ui->mapCmbBox->clear();
		for (int i = 1; i <= 9; i++)
			ui->mapCmbBox->addItem( QStringLiteral("E1M%1").arg(i) );
		for (int i = 1; i <= 9; i++)
			ui->mapCmbBox->addItem( QStringLiteral("E2M%1").arg(i) );
		for (int i = 1; i <= 9; i++)
			ui->mapCmbBox->addItem( QStringLiteral("E3M%1").arg(i) );
	} else if (!ui->mapCmbBox->itemText(0).startsWith('M')) {
		ui->mapCmbBox->clear();
		for (int i = 1; i <= 32; i++)
			ui->mapCmbBox->addItem( QStringLiteral("MAP%1").arg( i, 2, 10, QChar('0') ) );
	}
}

void MainWindow::updateListsFromDirs()
{
	if (iwadListFromDir) {
		updateIWADsFromDir( ui->iwadListView );
		iwadModel.updateView(0);
	}
	updateMapPacksFromDir();
	mapModel.updateView(0);
	updateSaveFilesFromDir();
	updateConfigFilesFromDir();
}


//----------------------------------------------------------------------------------------------------------------------
//  other options

void MainWindow::toggleAbsolutePaths( bool absolute )
{
	pathHelper.toggleAbsolutePaths( absolute );

	for (Engine & engine : engines)
		engine.path = pathHelper.convertPath( engine.path );

	if (iwadListFromDir && !iwadDir.isEmpty()) {
		iwadDir = pathHelper.convertPath( iwadDir );
	} else {
		for (IWAD & iwad : iwads)
			iwad.path = pathHelper.convertPath( iwad.path );
	}

	mapDir = pathHelper.convertPath( mapDir );

	modDir = pathHelper.convertPath( modDir );
	for (Mod & mod : mods)
		mod.path = pathHelper.convertPath( mod.path );

	for (Preset & preset : presets) {
		for (Mod & mod : preset.mods)
			mod.path = pathHelper.convertPath( mod.path );
		preset.selectedEnginePath = pathHelper.convertPath( preset.selectedEnginePath );
	}

	generateLaunchCommand();
}

void MainWindow::modeGameMenu()
{
	ui->mapCmbBox->setEnabled( false );
	ui->saveFileCmbBox->setEnabled( false );
	ui->skillCmbBox->setEnabled( false );
	ui->skillSpinBox->setEnabled( false );
	ui->noMonstersChkBox->setEnabled( false );
	ui->fastMonstersChkBox->setEnabled( false );
	ui->monstersRespawnChkBox->setEnabled( false );
	ui->dmFlagsBtn->setEnabled( false );
	ui->compatFlagsBtn->setEnabled( false );

	ui->multiplayerChkBox->setChecked( false );

	generateLaunchCommand();
}

void MainWindow::modeSelectedMap()
{
	ui->mapCmbBox->setEnabled( true );
	ui->saveFileCmbBox->setEnabled( false );
	ui->skillCmbBox->setEnabled( true );
	ui->skillSpinBox->setEnabled( ui->skillCmbBox->currentIndex() == Skill::CUSTOM );
	ui->noMonstersChkBox->setEnabled( true );
	ui->fastMonstersChkBox->setEnabled( true );
	ui->monstersRespawnChkBox->setEnabled( true );
	ui->dmFlagsBtn->setEnabled( true );
	ui->compatFlagsBtn->setEnabled( true );

	generateLaunchCommand();
}

void MainWindow::modeSavedGame()
{
	ui->mapCmbBox->setEnabled( false );
	ui->saveFileCmbBox->setEnabled( true );
	ui->skillCmbBox->setEnabled( false );
	ui->skillSpinBox->setEnabled( false );
	ui->noMonstersChkBox->setEnabled( false );
	ui->fastMonstersChkBox->setEnabled( false );
	ui->monstersRespawnChkBox->setEnabled( false );
	ui->dmFlagsBtn->setEnabled( false );
	ui->compatFlagsBtn->setEnabled( false );

	generateLaunchCommand();
}

void MainWindow::selectMap( QString )
{
	generateLaunchCommand();
}

void MainWindow::selectSavedGame( QString )
{
	generateLaunchCommand();
}

void MainWindow::selectSkill( int skill )
{
	ui->skillSpinBox->setValue( skill );
	ui->skillSpinBox->setEnabled( skill == Skill::CUSTOM );

	generateLaunchCommand();
}

void MainWindow::changeSkillNum( int skill )
{
	if (skill < Skill::CUSTOM)
		ui->skillCmbBox->setCurrentIndex( skill );

	generateLaunchCommand();
}

void MainWindow::toggleNoMonsters( bool )
{
	generateLaunchCommand();
}

void MainWindow::toggleFastMonsters( bool )
{
	generateLaunchCommand();
}

void MainWindow::toggleMonstersRespawn( bool )
{
	generateLaunchCommand();
}

void MainWindow::toggleMultiplayer( bool checked )
{
	ui->multRoleCmbBox->setEnabled( checked );
	ui->hostnameLine->setEnabled( checked );
	ui->portSpinBox->setEnabled( checked );
	ui->netModeCmbBox->setEnabled( checked );
	ui->gameModeCmbBox->setEnabled( checked );
	ui->playerCountSpinBox->setEnabled( checked );
	ui->teamDmgSpinBox->setEnabled( checked );
	ui->timeLimitSpinBox->setEnabled( checked );

	if (checked && ui->launchMode_menu->isChecked())
		ui->launchMode_map->click();

	generateLaunchCommand();
}

void MainWindow::selectMultRole( int )
{
	generateLaunchCommand();
}

void MainWindow::changeHost( QString )
{
	generateLaunchCommand();
}

void MainWindow::changePort( int )
{
	generateLaunchCommand();
}

void MainWindow::selectNetMode( int )
{
	generateLaunchCommand();
}

void MainWindow::selectGameMode( int )
{
	generateLaunchCommand();
}

void MainWindow::changePlayerCount( int )
{
	generateLaunchCommand();
}

void MainWindow::changeTeamDamage( double )
{
	generateLaunchCommand();
}

void MainWindow::changeTimeLimit( int )
{
	generateLaunchCommand();
}

void MainWindow::updateAdditionalArgs( QString )
{
	generateLaunchCommand();
}


//----------------------------------------------------------------------------------------------------------------------
//  saving & loading current options

void MainWindow::saveOptions( QString fileName )
{
	QFile file( fileName );
	if (!file.open( QIODevice::WriteOnly )) {
		QMessageBox::warning( this, "Can't open file", "Saving options failed. Could not open file "%fileName%" for writing." );
	}

	QJsonObject json;

	// window geometry
	{
		const QRect & geometry = this->geometry();
		json["width"] = geometry.width();
		json["height"] = geometry.height();
	}

	// engines
	{
		QJsonObject jsEngines;

		QJsonArray jsPortArray;
		for (const Engine & engine : engines) {
			QJsonObject jsPort;
			jsPort["name"] = engine.name;
			jsPort["path"] = engine.path;
			jsPortArray.append( jsPort );
		}
		jsEngines["engines"] = jsPortArray;

		json["engines"] = jsEngines;
	}

    // IWADs
	{
		QJsonObject jsIWADs;

		jsIWADs["auto_update"] = iwadListFromDir;
		jsIWADs["directory"] = iwadDir;
		QJsonArray jsIWADArray;
		for (const IWAD & iwad : iwads) {
			QJsonObject jsIWAD;
			jsIWAD["name"] = iwad.name;
			jsIWAD["path"] = iwad.path;
			jsIWADArray.append( jsIWAD );
		}
		jsIWADs["IWADs"] = jsIWADArray;

		json["IWADs"] = jsIWADs;
    }

    // map packs
	{
		QJsonObject jsMaps;
		jsMaps["directory"] = mapDir;
		json["maps"] = jsMaps;
	}

    // mods
	{
		QJsonObject jsMods;
		jsMods["directory"] = modDir;
		json["mods"] = jsMods;
	}

	// presets
	{
		QJsonArray jsPresetArray;

		for (const Preset & preset : presets) {
			QJsonObject jsPreset;

			jsPreset["name"] = preset.name;
			jsPreset["selected_engine"] = preset.selectedEnginePath;
			jsPreset["selected_config"] = preset.selectedConfig;
			jsPreset["selected_IWAD"] = preset.selectedIWAD;
			//jsPreset["selected_mappack"] = preset.selectedMapPack;
			QJsonArray jsModArray;
			for (const Mod & mod : preset.mods) {
				QJsonObject jsMod;
				jsMod["name"] = mod.name;
				jsMod["path"] = mod.path;
				jsMod["checked"] = mod.checked;
				jsModArray.append( jsMod );
			}
			jsPreset["mods"] = jsModArray;

			jsPresetArray.append( jsPreset );
		}

		json["presets"] = jsPresetArray;
	}

	json["use_absolute_paths"] = pathHelper.useAbsolutePaths();

	// additional command line arguments
	json["additional_args"] = ui->cmdArgsLine->text();

	// launch options
	json["dmflags1"] = qint64( dmflags1 );
	json["dmflags2"] = qint64( dmflags2 );
	json["compatflags1"] = qint64( compatflags1 );
	json["compatflags2"] = qint64( compatflags2 );

	// write the json to file
	QJsonDocument jsonDoc( json );
	file.write( jsonDoc.toJson() );
	file.close();

	//return file.error() == QFile::NoError;
}

void MainWindow::loadOptions( QString fileName )
{
	QFile file( fileName );
	if (!file.open( QIODevice::ReadOnly )) {
		QMessageBox::warning( this, "Can't open file", "Loading options failed. Could not open file "+fileName+" for reading." );
		return;
	}

	QJsonParseError error;
	QJsonDocument jsonDoc = QJsonDocument::fromJson( file.readAll(), &error );
	QJsonObject json = jsonDoc.object();

	// window geometry
	{
		width = getInt( json, "width", -1 );
		height = getInt( json, "height", -1 );
		if (shown && width > 0 && height > 0) {  // the window has been already shown and has the final position, so we can change the dimensions
			const QRect & geometry = this->geometry();
			this->setGeometry( geometry.x(), geometry.y(), width, height );
		} // otherwise we need to do this in showEvent callback
	}

	pathHelper.toggleAbsolutePaths( getBool( json, "use_absolute_paths", false ) );

	// engines
	{
		QJsonObject jsEngines = getObject( json, "engines" );

		ui->engineCmbBox->setCurrentIndex( -1 );
		engines.clear();

		QJsonArray jsEngineArray = getArray( jsEngines, "engines" );
		for (int i = 0; i < jsEngineArray.size(); i++)
		{
			QJsonObject jsEngine = getObject( jsEngineArray, i );
			if (jsEngine.isEmpty())  // wrong type on position i - skip this entry
				continue;

			QString name = getString( jsEngine, "name" );
			QString path = getString( jsEngine, "path" );
			if (name.isEmpty() || path.isEmpty())  // name or path doesn't exist - skip this entry
				continue;

			if (QFileInfo( path ).exists())
				engines.append({ name, pathHelper.convertPath( path ) });
			else
				QMessageBox::warning( this, "Engine no longer exists",
					"An engine from the saved options ("%path%") no longer exists. It will be removed from the list." );
		}

		engineModel.updateView(0);
	}

	// IWADS
	{
		QJsonObject jsIWADs = getObject( json, "IWADs" );

		deselectSelectedItems( ui->iwadListView );
		selectedIWADIdx = -1;
		iwads.clear();

		iwadListFromDir = getBool( jsIWADs, "auto_update", false );

		if (iwadListFromDir) {
			QString dir = getString( jsIWADs, "directory" );
			if (!dir.isEmpty()) {  // non-existing element directory - skip completely
				if (QDir( dir ).exists()) {
					iwadDir = pathHelper.convertPath( dir );
					updateIWADsFromDir( ui->iwadListView );
				} else {
					QMessageBox::warning( this, "IWAD dir no longer exists",
						"IWAD directory from the saved options ("%dir%") no longer exists. Please update it in Menu -> Setup." );
				}
			}
		} else {
			QJsonArray jsIWADArray = getArray( jsIWADs, "IWADs" );
			for (int i = 0; i < jsIWADArray.size(); i++)
			{
				QJsonObject jsIWAD = getObject( jsIWADArray, i );
				if (jsIWAD.isEmpty())  // wrong type on position i - skip this entry
					continue;

				QString name = getString( jsIWAD, "name" );
				QString path = getString( jsIWAD, "path" );
				if (name.isEmpty() || path.isEmpty())  // name or path doesn't exist - skip this entry
					continue;

				if (QFileInfo( path ).exists())
					iwads.append({ name, pathHelper.convertPath( path ) });
				else
					QMessageBox::warning( this, "IWAD no longer exists",
						"An IWAD from the saved options ("%path%") no longer exists. It will be removed from the list." );
			}
		}

		iwadModel.updateView(0);
	}

	// map packs
	{
		QJsonObject jsMaps = getObject( json, "maps" );

		deselectSelectedItems( ui->mapListView );
		selectedPackIdx = -1;
		maps.clear();

		QString dir = getString( jsMaps, "directory" );
		if (!dir.isEmpty()) {  // non-existing element directory - skip completely
			if (QDir( dir ).exists()) {
				mapDir = pathHelper.convertPath( dir );
				updateMapPacksFromDir();
			} else {
				QMessageBox::warning( this, "Map dir no longer exists",
					"Map directory from the saved options ("%dir%") no longer exists. Please update it in Menu -> Setup." );
			}
		}

		mapModel.updateView(0);
	}

	// mods
	{
		QJsonObject jsMods = getObject( json, "mods" );

		deselectSelectedItems( ui->modListView );

		QString dir = getString( jsMods, "directory" );
		if (!dir.isEmpty()) {  // non-existing element directory - skip completely
			if (QDir( dir ).exists())
				modDir = dir;
			else
				QMessageBox::warning( this, "Mod dir no longer exists",
					"Mod directory from the saved options ("%dir%") no longer exists. Please update it in Menu -> Setup." );
		}
	}

	// presets
	{
		QJsonArray jsPresetArray = getArray( json, "presets" );

		deselectSelectedItems( ui->presetListView );
		presets.clear();

		for (int i = 0; i < jsPresetArray.size(); i++)
		{
			QJsonObject jsPreset = getObject( jsPresetArray, i );
			if (jsPreset.isEmpty())  // wrong type on position i - skip this entry
				continue;

			Preset preset;
			preset.name = getString( jsPreset, "name", "<missing name>" );
			preset.selectedEnginePath = pathHelper.convertPath( getString( jsPreset, "selected_engine" ) );
			preset.selectedConfig = getString( jsPreset, "selected_config" );
			preset.selectedIWAD = getString( jsPreset, "selected_IWAD" );
			//preset.selectedMapPack = getString( jsPreset, "selected_mappack" );
			QJsonArray jsModArray = getArray( jsPreset, "mods" );
			for (int i = 0; i < jsModArray.size(); i++)
			{
				QJsonObject jsMod = getObject( jsModArray, i );
				if (jsMod.isEmpty())  // wrong type on position i - skip this entry
					continue;

				Mod mod;
				mod.name = getString( jsMod, "name" );
				mod.path = getString( jsMod, "path" );
				mod.checked = getBool( jsMod, "checked", false );
				if (!mod.name.isEmpty() && !mod.path.isEmpty()) {
					mod.path = pathHelper.convertPath( mod.path );
					preset.mods.append( mod );
				}
			}
			presets.append( preset );
		}

		presetModel.updateView(0);
	}

	ui->cmdArgsLine->setText( getString( json, "additional_args" ) );

	// launch options
	dmflags1 = getUInt( json, "dmflags1", 0 );
	dmflags2 = getUInt( json, "dmflags2", 0 );
	compatflags1 = getUInt( json, "compatflags1", 0 );
	compatflags2 = getUInt( json, "compatflags2", 0 );

	file.close();

	updateListsFromDirs();

	generateLaunchCommand();
}

void MainWindow::exportPreset()
{
	int selectedIdx = getSelectedItemIdx( ui->presetListView );
	if (selectedIdx < 0) {
		QMessageBox::warning( this, "No preset selected", "Select a preset from the preset list." );
		return;
	}

	QString fileName = QFileDialog::getSaveFileName( this, "Save preset", QString(), scriptFileExt );
	if (fileName.isEmpty()) {  // user probably clicked cancel
		return;
	}

	QFile file( fileName );
	if (!file.open( QIODevice::ReadWrite )) {
		QMessageBox::warning( this, "Cannot open file", "Cannot open file for writing. Check directory permissions" );
		return;
	}

	QTextStream stream( &file );

	// TODO: convert paths according to the target directory
	stream << ui->commandLine->text() << endl;

	file.close();
}

void MainWindow::importPreset()
{
	QMessageBox::warning( this, "Not implemented", "Sorry, this feature is not implemented yet" );
}


//----------------------------------------------------------------------------------------------------------------------
//  launch command generation

void MainWindow::generateLaunchCommand()
{
	QString curCommand = ui->commandLine->text();
	QString newCommand;
	QTextStream cmdStream( &newCommand );

	const int engineIdx = ui->engineCmbBox->currentIndex();
	if (engineIdx >= 0) {
		cmdStream << "\"" << engines[ engineIdx ].path << "\"";

		const int configIdx = ui->configCmbBox->currentIndex();
		if (configIdx >= 0)
			cmdStream << "-config \""
			          << QFileInfo( engines[ engineIdx ].path ).dir().filePath( ui->configCmbBox->currentText() ) << "\"";
	}

	const int iwadIdx = getSelectedItemIdx( ui->iwadListView );
	if (iwadIdx >= 0)
		cmdStream << " -iwad \"" << iwads[ iwadIdx ].path << "\"";

	const int mapIdx = getSelectedItemIdx( ui->mapListView );
	if (mapIdx >= 0)
		cmdStream << " -file \"" << QDir( mapDir ).filePath( maps[ mapIdx ].name ) << "\"";

	for (const Mod & mod : mods)
		if (mod.checked)
			cmdStream << " -file \"" << mod.path << "\"";

	if (ui->launchMode_map->isChecked()) {
		cmdStream << " -warp " << getMapNumber( ui->mapCmbBox->currentText() );
		cmdStream << " -skill " << ui->skillSpinBox->text();
		if (ui->noMonstersChkBox->isChecked())
			cmdStream << " -nomonsters";
		if (ui->fastMonstersChkBox->isChecked())
			cmdStream << " -fast";
		if (ui->monstersRespawnChkBox->isChecked())
			cmdStream << " -respawn";
		if (dmflags1 != 0)
			cmdStream << " +dmflags " << QString::number( dmflags1 );
		if (dmflags2 != 0)
			cmdStream << " +dmflags2 " << QString::number( dmflags2 );
		if (compatflags1 != 0)
			cmdStream << " +compatflags " << QString::number( compatflags1 );
		if (compatflags2 != 0)
			cmdStream << " +compatflags2 " << QString::number( compatflags2 );
	} else if (ui->launchMode_savefile->isChecked()) {
		cmdStream << " -loadgame " << ui->saveFileCmbBox->currentText();
	}

	if (ui->multiplayerChkBox->isChecked()) {
		switch (ui->multRoleCmbBox->currentIndex()) {
		 case MultRole::SERVER:
			cmdStream << " -host " << ui->playerCountSpinBox->text();
			if (ui->portSpinBox->value() != 5029)
				cmdStream << " -port " << ui->portSpinBox->text();
			switch (ui->gameModeCmbBox->currentIndex()) {
			 case DEATHMATCH:
				cmdStream << " -deathmatch";
				break;
			 case TEAM_DEATHMATCH:
				cmdStream << " -deathmatch +teamplay";
				break;
			 case ALT_DEATHMATCH:
				cmdStream << " -altdeath";
				break;
			 case ALT_TEAM_DEATHMATCH:
				cmdStream << " -altdeath +teamplay";
				break;
			 case COOPERATIVE: // default mode, which is started without any param
				break;
			 default:
				QMessageBox::critical( this, "Invalid game mode index",
					"The game mode index is out of range. This shouldn't be happening and it is a bug. Please create an issue on Github page." );
			}
			if (ui->teamDmgSpinBox->value() != 0.0)
				cmdStream << " +teamdamage " << QString::number( ui->teamDmgSpinBox->value(), 'f', 2 );
			if (ui->timeLimitSpinBox->value() != 0)
				cmdStream << " -timer " << ui->timeLimitSpinBox->text();
			cmdStream << " -netmode " << QString::number( ui->netModeCmbBox->currentIndex() );
			break;
		 case MultRole::CLIENT:
			cmdStream << " -join " << ui->hostnameLine->text() << ":" << ui->portSpinBox->text();
			break;
		 default:
			QMessageBox::critical( this, "Invalid multiplayer role index",
				"The multiplayer role index is out of range. This shouldn't be happening and it is a bug. Please create an issue on Github page." );
		}
	}

	if (!ui->cmdArgsLine->text().isEmpty())
		cmdStream << " " << ui->cmdArgsLine->text();

	cmdStream.flush();

	// Don't replace the line widget's content if there is no change. It would just annoy a user who is trying to select
	// and copy part of the line, by constantly reseting his selection.
	if (newCommand != curCommand)
		ui->commandLine->setText( newCommand );
}

void MainWindow::launch()
{
	if (ui->engineCmbBox->currentIndex() < 0) {
		QMessageBox::warning( this, "No engine selected", "No Doom engine is selected." );
		return;
	}

	bool success = QProcess::startDetached( ui->commandLine->text() );
	if (!success) {
		QMessageBox::warning( this, tr("Launch error"), tr("Failed to execute launch command.") );
	}
}