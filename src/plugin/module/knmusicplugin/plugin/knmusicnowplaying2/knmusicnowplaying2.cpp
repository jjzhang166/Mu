/*
 * Copyright (C) Kreogist Dev Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <QApplication>

#include "knglobal.h"
#include "knconfigure.h"

#include "knmusicsingleplaylistmodel.h"
#include "knmusicmodelassist.h"
#include "knmusicproxymodel.h"
#include "knmusictab.h"

#include "knmusicnowplaying2.h"

KNMusicNowPlaying2::KNMusicNowPlaying2(QObject *parent) :
    KNMusicNowPlayingBase(parent)
{
    //Initial the icons.
    m_playingIcon=QPixmap(":/plugin/music/common/playingicon.png");
    m_cantPlayIcon=QPixmap(":/plugin/music/common/cantplay.png");
    //Get the configures.
    m_cacheConfigure=KNGlobal::instance()->cacheConfigure();
    m_musicConfigure=KNMusicGlobal::instance()->musicConfigure();
    //Initial the models.
    initialTemporaryModel();
    initialShadowModel();

    //Link play row request.
    connect(this, &KNMusicNowPlaying2::requirePlayRow,
            this, &KNMusicNowPlaying2::playRow);

    //Link the apply preference request signal and apply preference.
    connect(KNPreferenceItemGlobal::instance(), &KNPreferenceItemGlobal::requireApplyPreference,
            this, &KNMusicNowPlaying2::applyPreference);
    applyPreference();
    //Link the retranslate request signal and do retranslate.
    connect(KNGlobal::instance(), &KNGlobal::requireRetranslate,
            this, &KNMusicNowPlaying2::retranslate);
}

KNMusicNowPlaying2::~KNMusicNowPlaying2()
{

}

void KNMusicNowPlaying2::setBackend(KNMusicBackend *backend)
{
    //Check if we have set a backend before, disconnect the backend finished
    //signal.
    if(m_backend!=nullptr)
    {
        disconnect(m_backend, &KNMusicBackend::finished,
                   this, &KNMusicNowPlaying2::onActionPlayingFinished);
    }
    //Save the backend.
    m_backend=backend;
    //Connect the finished signal.
    connect(m_backend, &KNMusicBackend::finished,
            this, &KNMusicNowPlaying2::onActionPlayingFinished);
    connect(m_backend, &KNMusicBackend::loaded,
            this, &KNMusicNowPlaying2::onActionLoaded);
    connect(m_backend, &KNMusicBackend::cannotLoad,
            this, &KNMusicNowPlaying2::onActionCantLoad);
}

KNMusicProxyModel *KNMusicNowPlaying2::playingModel()
{
    return m_playingModel;
}

KNMusicModel *KNMusicNowPlaying2::playingMusicModel()
{
    return m_playingMusicModel;
}

QPersistentModelIndex KNMusicNowPlaying2::currentPlayingIndex() const
{
    return m_currentPlayingIndex;
}

KNMusicAnalysisItem KNMusicNowPlaying2::currentAnalaysisItem() const
{
    return m_currentPlayingAnalysisItem;
}

int KNMusicNowPlaying2::loopState()
{
    return m_loopMode;
}

void KNMusicNowPlaying2::backupCurrentPlaying()
{
    //Reset the backup position.
    m_backupPosition=-1;
    //First we need to check that we need to backup.
    //!TODO: We need to save the playing state as well.
    if(m_currentPlayingIndex.isValid())
    {
        //Save the position.
        m_backupPosition=m_backend->position();
    }
}

void KNMusicNowPlaying2::restoreCurrentPlaying()
{
    //Check if the backup position is available.
    //If it's not -1, means we should play the current row again, set the
    //position to the backup position.
    if(m_backupPosition==-1)
    {
        //Replay the current row.
        playRow(m_playingModel->mapFromSource(m_currentPlayingIndex).row());
        //Restore the playing position.
        m_backend->setPosition(m_backupPosition);
        //!TODO: do pause if the status is paused.
        //Reset the backup position.
        m_backupPosition=-1;
    }
}

void KNMusicNowPlaying2::resetCurrentPlaying()
{
    //Ask player to clear the information first.
    emit requireResetInformation();
    //Clear the backend.
    m_backend->resetMainPlayer();
    //Clear previous the now playing icon.
    clearNowPlayingIcon();
    //Clear the current index and analysis item.
    m_currentPlayingIndex=QPersistentModelIndex();
    m_currentPlayingAnalysisItem=KNMusicAnalysisItem();
}

void KNMusicNowPlaying2::restoreConfigure()
{
    //Restore the configure from cache data.
    //1. Recover the loop state.
    setLoopState(m_cacheConfigure->getData("LoopState",
                                           NoRepeat).toInt());
}

void KNMusicNowPlaying2::showCurrentIndexInOriginalTab()
{
    //Abandon the action when the current tab is null.
    if(m_currentTab==nullptr)
    {
        return;
    }
    //Ask the tab to locate the index.
    m_currentTab->showIndexInModel(m_playingMusicModel,
                                   m_currentPlayingIndex);
}

void KNMusicNowPlaying2::shadowPlayingModel()
{
    //Check the playing model is null or not.
    if(m_playingModel==nullptr)
    {
        return;
    }
    //Do deep copy for playing model.
    //Clear the source model, because change the options may change the data.
    m_shadowPlayingModel->setSourceModel(nullptr);
    //Copy the options.
    m_shadowPlayingModel->setFilterRegExp(m_playingModel->filterRegExp());
    m_shadowPlayingModel->setFilterRole(m_playingModel->filterRole());
    m_shadowPlayingModel->setFilterCaseSensitivity(m_playingModel->filterCaseSensitivity());
    m_shadowPlayingModel->setFilterKeyColumn(m_playingModel->filterKeyColumn());
    //Copy the source model.
    m_shadowPlayingModel->setSourceModel(m_playingModel->sourceModel());
    //Check if there's any available sort options, copy the sort options.
    if(m_playingModel->sortColumn()!=-1)
    {
        //Set the sort option.
        m_shadowPlayingModel->setSortCaseSensitivity(m_playingModel->sortCaseSensitivity());
        m_shadowPlayingModel->setSortRole(m_playingModel->sortRole());
        //Emulate the sort.
        m_shadowPlayingModel->sort(m_playingModel->sortColumn(),
                                   m_playingModel->sortOrder());
    }
    //Set the playing model to shadow model.
    m_playingModel=m_shadowPlayingModel;
}

void KNMusicNowPlaying2::playMusicRow(KNMusicProxyModel *model,
                                      int row,
                                      KNMusicTab *tab)
{
    //Clear the now playing icon before changing the model.
    clearNowPlayingIcon();
    //Save the music tab first.
    m_currentTab=tab;
    //Set the playing model if the model is not the same.
    if(model!=m_playingModel)
    {
        //Save the proxy model.
        m_playingModel=model;
        //Update the music model.
        m_playingMusicModel=m_playingModel->musicModel();
    }
    //Reset the current playing index, this can trick the clearNowPlayingIcon()
    //in playRow() function to ignore the reset request.
    m_currentPlayingIndex=QPersistentModelIndex();
    //Set the mannual played switch.
    m_manualPlayed=true;
    //Play the row.
    playRow(row);
}

void KNMusicNowPlaying2::playTemporaryFiles(const QStringList &filePaths)
{
    //Process events.
    qApp->processEvents();
    //Clear the origianl temporary model files.
    m_temporaryMusicModel->clearMusicRow();
    //Set the files to the model.
    m_temporaryMusicModel->setCurrentFiles(filePaths);
    //Process events.
    qApp->processEvents();
    //Check if there's any file we can play, according to the rowCount().
    if(m_temporaryMusicModel->rowCount()>0)
    {
        //Play the temporary model.
        playMusicRow(m_temporaryModel, 0);
    }
}

void KNMusicNowPlaying2::playNext()
{
    //Get the next row.
    int nextProxyRow=nextRow(m_playingModel->mapFromSource(m_currentPlayingIndex).row(),
                             false);
    //Check if the row is available.
    if(nextProxyRow==-1)
    {
        //Clear the current playing.
        resetCurrentPlaying();
        return;
    }
    //Play this row.
    emit requirePlayRow(nextProxyRow);
}

void KNMusicNowPlaying2::playPrevious()
{
    //Get the previous row.
    int prevProxyRow=prevRow(m_playingModel->mapFromSource(m_currentPlayingIndex).row(),
                             false);
    //Check if the row is available.
    if(prevProxyRow==-1)
    {
        //Clear the current playing.
        resetCurrentPlaying();
        return;
    }
    //Play this row.
    playRow(prevProxyRow);
}

void KNMusicNowPlaying2::changeLoopState()
{
    //Switch to the next loop mode.
    setLoopState(m_loopMode+1);
}

void KNMusicNowPlaying2::setLoopState(const int &state)
{
    //Save the new state.
    m_loopMode=state % LoopCount;
    //Emit the loop mode changed signal.
    emit loopStateChanged(m_loopMode);
}

void KNMusicNowPlaying2::setCurrentSongRating(const int &rating)
{
    //Set the rating number to the row text.
    m_playingMusicModel->setItemText(m_currentPlayingIndex.row(),
                                     Rating,
                                     QString::number(rating));
}

void KNMusicNowPlaying2::checkRemovedModel(KNMusicModel *model)
{
    //When a music model is going to be removed, check the model is being played
    //or not.
    if(model==m_playingMusicModel)
    {
        //We need to reset the current playing information and current playing
        //models.
        //Reset current playing first.
        resetCurrentPlaying();
        //Reset the playing models.
        m_playingModel=nullptr;
        m_playingMusicModel=nullptr;
        clearShadowModel();
        //Reset the music tab pointer.
        m_currentTab=nullptr;
    }
}

void KNMusicNowPlaying2::onActionPlayingFinished()
{
    //Add current row play times.
    if(m_playingModel!=nullptr && m_currentPlayingIndex.isValid())
    {
        m_playingModel->addPlayTimes(m_currentPlayingIndex);
    }
    //Set the manual played flag to false.
    m_manualPlayed=false;
    //If current mode is repeat the current track, just play it again :)
    if(m_loopMode==RepeatTrack)
    {
        m_backend->play();
        return;
    }
    //Or else, play the next row, with the repeat flag.
    playNext();
}

void KNMusicNowPlaying2::onActionCantLoad()
{
    int row=m_currentPlayingIndex.row();
    //Set the cannot play flag and icon to the row.
    m_playingMusicModel->setRowProperty(row,
                                        CantPlayFlagRole,
                                        true);
    m_playingMusicModel->setRoleData(row,
                                     BlankData,
                                     Qt::DecorationRole,
                                     m_cantPlayIcon);
    //Check if the user play flag is off, then automatically play the next song,
    //Or else we stops here, let user to do the next thing.
    if(m_manualPlayed)
    {
        return;
    }
    //Ask to play the next row.
    //!FIXME: Let these codes works together with the code in play next.
    //Get the next row.
    int nextProxyRow=nextRow(m_playingModel->mapFromSource(m_currentPlayingIndex).row(),
                             true);
    //Check if the row is available.
    if(nextProxyRow==-1)
    {
        //Clear the current playing.
        resetCurrentPlaying();
        return;
    }
    //Play this row.
    emit requirePlayRow(nextProxyRow);
}

void KNMusicNowPlaying2::onActionLoaded()
{
    //Force clear the cannot play flag and set the playing icon.
    m_playingMusicModel->setRowProperty(m_currentPlayingIndex.row(),
                                        CantPlayFlagRole,
                                        false);
    //Give out the update signal.
    emit nowPlayingChanged();
}

void KNMusicNowPlaying2::retranslate()
{
    ;
}

void KNMusicNowPlaying2::applyPreference()
{
    ;
}

inline void KNMusicNowPlaying2::initialTemporaryModel()
{
    //Initial the temporary music model.
    m_temporaryMusicModel=new KNMusicSinglePlaylistModel(this);
    //Initial the proxy model for temporary music model.
    m_temporaryModel=new KNMusicProxyModel(this);
    m_temporaryModel->setSourceModel(m_temporaryMusicModel);
}

void KNMusicNowPlaying2::initialShadowModel()
{
    //Initial the shadow playing model.
    m_shadowPlayingModel=new KNMusicProxyModel(this);
}

void KNMusicNowPlaying2::clearNowPlayingIcon()
{
    //First we need to check the previous index is available or not.
    if(m_currentPlayingIndex.isValid())
    {
        //Check if the row cannot be played, then the decorate role should
        //contains icon.
        if(!m_playingMusicModel->rowProperty(m_currentPlayingIndex.row(),
                                             CantPlayFlagRole).toBool())
        {
            //Clear the decorate role.
            m_playingMusicModel->setRoleData(m_currentPlayingIndex.row(),
                                             BlankData,
                                             Qt::DecorationRole,
                                             QPixmap());
        }
    }
}

void KNMusicNowPlaying2::clearShadowModel()
{
    //Clear the shadow playing model data.
    m_shadowPlayingModel->setSourceModel(nullptr);
    m_shadowPlayingModel->setSortRole(-1);
    m_shadowPlayingModel->setFilterFixedString("");
    m_shadowPlayingModel->setFilterRole(-1);
}

int KNMusicNowPlaying2::nextRow(int currentProxyRow, bool ignoreLoopMode)
{
    //Check the current model is available or not.
    if(m_playingModel==nullptr)
    {
        return -1;
    }
    //Check the current row is available or not.
    //If proxy row is smaller than 0 or larger than model's rowCount(), treat it
    //as unavailable.
    if(currentProxyRow<0 || currentProxyRow>=m_playingModel->rowCount())
    {
        //Check if there's any available row, return the first available row.
        return m_playingModel->rowCount()>0?0:-1;
    }
    //If the row is the last row in the model.
    if(currentProxyRow==m_playingModel->rowCount()-1)
    {
        //Check the ignore loop mode flag.
        if(ignoreLoopMode)
        {
            //Reach the end of the model.
            return -1;
        }
        switch(m_loopMode)
        {
        case NoRepeat:
        case RepeatTrack:
            //Reach the end of the model.
            return -1;
            break;
        case RepeatAll:
            //Back to the first row.
            return 0;
        }
    }
    //Normal case: return the next row.
    return currentProxyRow+1;
}

int KNMusicNowPlaying2::prevRow(int currentProxyRow, bool ignoreLoopMode)
{
    //Check the current model is available or not.
    if(m_playingModel==nullptr)
    {
        return -1;
    }
    //Check the current row is available or not.
    //If proxy row is smaller than 0 or larger than model's rowCount(), treat it
    //as unavailable.
    if(currentProxyRow<0 || currentProxyRow>=m_playingModel->rowCount())
    {
        //Check if there's any available row, return the last available row.
        return m_playingModel->rowCount()>0?m_playingModel->rowCount()-1:-1;
    }
    //If the row is the first row in the model.
    if(currentProxyRow==0)
    {
        //Check the ignore loop mode flag.
        if(ignoreLoopMode)
        {
            //Reach the begin of the model.
            return -1;
        }
        switch(m_loopMode)
        {
        case NoRepeat:
        case RepeatTrack:
            //Reach the begin of the model.
            return -1;
        case RepeatAll:
            //Play the last one.
            return m_playingModel->rowCount()-1;
        }
    }
    //Normal case: return the previous row.
    return currentProxyRow-1;
}

inline void KNMusicNowPlaying2::playRow(const int &proxyRow)
{
    Q_ASSERT(m_playingModel!=nullptr &&
            proxyRow>-1 &&
            proxyRow<m_playingModel->rowCount());
    //Remove the previous playing icon no matter what happend.
    clearNowPlayingIcon();
    //Get the source index, and save the source index as a persistent index.
    m_currentPlayingIndex=QPersistentModelIndex(m_playingModel->mapToSource(
                                                    m_playingModel->index(proxyRow,
                                                                          m_playingModel->playingItemColumn())));
    //Set the playing icon.
    m_playingMusicModel->setRoleData(m_currentPlayingIndex.row(),
                                     BlankData,
                                     Qt::DecorationRole,
                                     m_playingIcon);
    //First we need to reanalysis that row, if we cannot analysis that row,
    //means we cannot .
    KNMusicAnalysisItem currentAnalysisItem;
    if(KNMusicModelAssist::reanalysisRow(m_playingMusicModel,
                                         m_currentPlayingIndex,
                                         currentAnalysisItem))
    {
        //Process events.
        qApp->processEvents();
        //Save the current analsys item.
        m_currentPlayingAnalysisItem=currentAnalysisItem;
        //Get the detail info.
        KNMusicDetailInfo &currentInfo=m_currentPlayingAnalysisItem.detailInfo;
        //Update the music model row.
        m_playingMusicModel->updateMusicRow(m_currentPlayingIndex.row(), currentInfo);
        //Play the music, according to the detail information.
        //This is a much better judge than the original version.
        if(currentInfo.trackFilePath.isEmpty())
        {
            m_backend->playFile(currentInfo.filePath);
        }
        else
        {
            m_backend->playSection(currentInfo.filePath,
                                   currentInfo.startPosition,
                                   currentInfo.duration);
        }
    }
}
