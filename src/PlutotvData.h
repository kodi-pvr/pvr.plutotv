/*
 *  Copyright (C) 2020 flubshi (https://github.com/flubshi)
 *  Copyright (C) 2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2025 Shawn Ray (https://github.com/Asmodasis)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "kodi/addon-instance/PVR.h"
#include "guilib/GUIDialog.h"
//#include "rapidjson/document.h"
#include <nlohmann/json.hpp>
//#include <ChannelGroups.h>

#include <memory>
#include <vector>
#include <algorithm>

/**
 * User Agent for HTTP Requests
 */
static const std::string PLUTOTV_USER_AGENT =
    "Mozilla/5.0 (Windows NT 6.2; rv:24.0) Gecko/20100101 Firefox/24.0";

class ATTR_DLL_LOCAL PlutotvData : public kodi::addon::CAddonBase,
                                   public kodi::addon::CInstancePVRClient,
                                   public kodi::addon::CGUIMessage
{
public:

  PlutotvData();
  ~PlutotvData();

  std::string HttpGet(const std::string&);
  
  PlutotvData(const PlutotvData&) = delete;
  PlutotvData(PlutotvData&&) = delete;
  PlutotvData& operator=(const PlutotvData&) = delete;
  PlutotvData& operator=(PlutotvData&&) = delete;

  ADDON_STATUS Create() override;
  ADDON_STATUS SetSetting(const std::string& settingName,
                          const kodi::addon::CSettingValue& settingValue) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;

  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                   kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      std::vector<kodi::addon::PVRStreamProperty>& properties);

  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;

  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR GetRecordingStreamProperties(const kodi::addon::PVRRecording& recording,
                                                       std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  std::string GetRecordingURL(const kodi::addon::PVRRecording& recording);

  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;

  PVR_ERROR CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                            const kodi::addon::PVREPGTag& item) override;
  PVR_ERROR CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                const kodi::addon::PVRChannel& item) override;
  PVR_ERROR CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                              const kodi::addon::PVRTimer& item) override;
  PVR_ERROR CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                  const kodi::addon::PVRRecording& item) override;
  PVR_ERROR CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook) override;


private:
  struct PlutotvChannel
  {
    int iUniqueId;
    std::string plutotvID;
    int iChannelNumber; //position
    std::string strChannelName;
    //std::string strChannelExtraInfo;        // Extra information about the channel
    //std::string strChannelShowDescription;  // Description about the show being played
    std::string strIconPath;
    std::string strStreamURL;
    // The group the channel belongs to
    kodi::addon::PVRChannelGroup m_Groups;
  };
  struct PlutotvRecording
  {
    bool bRadio;
    int iDuration;
    int iGenreType;
    int iGenreSubType;
    int iSeriesNumber;
    int iEpisodeNumber;
    int iEpisodePartNumber;
    std::string strChannelName;
    std::string strPlotOutline;
    std::string strPlot;
    std::string strRecordingId;
    std::string strStreamURL;
    std::string strTitle;
    std::string strEpisodeName;
    std::string strDirectory;
    time_t recordingTime;
    int iChannelId;
    int iProviderId;
    int iParentalRating = 0;
    std::string strParentalRatingCode;
    std::string strParentalRatingIcon;
    std::string strParentalRatingSource;
    int iYear;
  };
  struct PlutotvTimer
  {
    int iChannelId;
    time_t startTime;
    time_t endTime;
    PVR_TIMER_STATE state;
    std::string strTitle;
    std::string strSummary;
  };

  std::shared_ptr<nlohmann::json> m_epg_cache_document;
  time_t m_epg_cache_start = time_t(0);
  time_t m_epg_cache_end = time_t(0);;
  std::string strUrl;

  std::vector<PlutotvChannel> m_channels;
  // List of unique group elements 
  std::vector<std::string> uniqueGroupList;
  // List of recordings on the backend
  //std::vector<kodi::addon::PVRRecording> m_recordings;
  std::vector<PlutotvRecording> m_recordings;
  std::vector<PlutotvRecording> m_recordingsDeleted;
  std::vector<PlutotvTimer> m_timers;
  bool m_bChannelsLoaded = false;
  int categoryCounter; 

  PVR_ERROR CallMenuHook(const kodi::addon::PVRMenuhook& menuhook);

  std::string GetChannelStreamURL(int uniqueId);
  std::string GetSettingsUUID(const std::string& setting);
  int GetSettingsStartChannel() const;
  bool GetSettingsColoredChannelLogos() const;
  bool GetSettingsWorkaroundBrokenStreams() const;
  void SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                           const std::string& url,
                           bool realtime);
  bool LoadChannelsData();
};
