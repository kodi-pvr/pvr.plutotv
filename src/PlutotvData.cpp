/*
 *  Copyright (C) 2020 flubshi (https://github.com/flubshi)
 *  Copyright (C) 2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PlutotvData.h"

#include "Curl.h"
#include "Utils.h"
#include "kodi/tools/StringUtils.h"

#include <cctype>
#include <ctime>
#include <iomanip>
#include <ios>
#include <sstream>

ADDON_STATUS PlutotvData::Create()
{
  kodi::Log(ADDON_LOG_DEBUG, "%s - Creating the pluto.tv PVR add-on", __FUNCTION__);
  return ADDON_STATUS_OK;
}

ADDON_STATUS PlutotvData::SetSetting(const std::string& settingName,
                                     const kodi::addon::CSettingValue& settingValue)
{
  return ADDON_STATUS_NEED_RESTART;
}

PVR_ERROR PlutotvData::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PlutotvData::GetBackendName(std::string& name)
{
  name = "pluto.tv PVR add-on";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PlutotvData::GetBackendVersion(std::string& version)
{
  version = STR(IPTV_VERSION);
  return PVR_ERROR_NO_ERROR;
}

namespace
{
// http://stackoverflow.com/a/17708801
const std::string UrlEncode(const std::string& value)
{
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (auto c : value)
  {
    // Keep alphanumeric and other accepted characters intact
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
    {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
  }

  return escaped.str();
}
} // unnamed namespace

void PlutotvData::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                      const std::string& url,
                                      bool realtime)
{
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", url.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");
  // HLS
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/x-mpegURL");

  const std::string encodedUserAgent{UrlEncode(PLUTOTV_USER_AGENT)};
  properties.emplace_back("inputstream.adaptive.manifest_headers",
                          "User-Agent=" + encodedUserAgent);
  properties.emplace_back("inputstream.adaptive.stream_headers", "User-Agent=" + encodedUserAgent);

  if (GetSettingsWorkaroundBrokenStreams())
    properties.emplace_back("inputstream.adaptive.manifest_config",
                            "{\"hls_ignore_endlist\":true,\"hls_fix_mediasequence\":true,\"hls_fix_"
                            "discsequence\":true}");
}

bool PlutotvData::LoadChannelsData()
{
  if (m_bChannelsLoaded)
    return true;

  GetJWT();
  if (m_jwt.empty())
    return false;

  kodi::Log(ADDON_LOG_DEBUG, "[load data] GET CHANNELS");

  const std::string jsonChannels{GetChannelsJson()};

  if (jsonChannels.empty() || jsonChannels == "[]")
  {
    kodi::Log(ADDON_LOG_ERROR, "[channels] ERROR - empty response");
    return false;
  }

  kodi::Log(ADDON_LOG_DEBUG, "[channels] length: %i;", jsonChannels.size());
  kodi::Log(ADDON_LOG_DEBUG, "[channels] %s;", jsonChannels.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "[channels] %s;",
            jsonChannels.substr(jsonChannels.size() - 40).c_str());

  // parse channels
  kodi::Log(ADDON_LOG_DEBUG, "[channels] parse channels");
  nlohmann::json channelsDoc = nlohmann::json::parse(jsonChannels.c_str());
  if (channelsDoc.is_discarded())
  {
    kodi::Log(ADDON_LOG_ERROR, "[LoadChannelData] ERROR: error while parsing json");
    return false;
  }
  kodi::Log(ADDON_LOG_DEBUG, "[channels] iterate channels");
  kodi::Log(ADDON_LOG_DEBUG, "[channels] size: %i;", channelsDoc.at("data").size());

  // Use configured start channel number to populate the channel list
  int i = GetSettingsStartChannel();
  for (const auto& channel : channelsDoc.at("data"))
  {
    const std::string plutotvid{channel.at("id")};

    PlutotvChannel plutotv_channel;
    plutotv_channel.iChannelNumber = i++; // position
    kodi::Log(ADDON_LOG_DEBUG, "[channel] channelnr(pos): %i;", plutotv_channel.iChannelNumber);

    plutotv_channel.plutotvID = plutotvid;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] pluto.tv ID: %s;", plutotv_channel.plutotvID.c_str());

    const int uniqueId = Utils::Hash(plutotvid);
    plutotv_channel.iUniqueId = uniqueId;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] id: %i;", uniqueId);

    const std::string displayName = channel.at("name");
    plutotv_channel.strChannelName = displayName;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] name: %s;", plutotv_channel.strChannelName.c_str());

    std::string logo;

    if (channel.contains("images") && channel.at("images").size() > 0)
    {
      for (const auto& l : channel.at("images"))
      {
        if (!l.contains("type"))
          continue;

        if (GetSettingsColoredChannelLogos())
        {
          if (l.at("type") == "colorLogoPNG")
          {
            logo = l.at("url");
            break;
          }
        }
        else if (l.at("type") == "solidLogoPNG")
        {
          logo = l.at("url");
          break;
        }
        // fallback, should always work
        if (l.at("type") == "logo")
        {
          logo = l.at("url");
          // no break, since we might find the proper image with one of the next iterations
        }
      }
    }

    plutotv_channel.strIconPath = logo;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] iconpath: %s;", plutotv_channel.strIconPath.c_str());

    if (channel.contains("stitched") && channel.at("stitched").contains("paths") &&
        channel.at("stitched").at("paths").size() > 0)
    {
      std::string streamURL{"https://cfd-v4-service-channel-stitcher-use1-1.prd.pluto.tv/v2"};
      streamURL += channel.at("stitched").at("paths").at(0).at("path");
      streamURL += "?includeExtendedEvents=true";
      streamURL += "&masterJWTPassthrough=true";
      streamURL += "&jwt="; // JWT value will be added on demand.

      plutotv_channel.strStreamURL = streamURL;
      kodi::Log(ADDON_LOG_DEBUG, "[channel] streamURL: %s;", streamURL.c_str());
    }

    m_channels.emplace_back(plutotv_channel);
  }

  m_bChannelsLoaded = true;
  return true;
}

PVR_ERROR PlutotvData::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "pluto.tv function call: [%s]", __FUNCTION__);

  LoadChannelsData();
  if (!m_bChannelsLoaded)
    return PVR_ERROR_SERVER_ERROR;

  amount = static_cast<int>(m_channels.size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PlutotvData::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "pluto.tv function call: [%s]", __FUNCTION__);

  if (!radio)
  {
    LoadChannelsData();
    if (!m_bChannelsLoaded)
      return PVR_ERROR_SERVER_ERROR;

    for (const auto& channel : m_channels)
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(false);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(false);

      results.Add(kodiChannel);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PlutotvData::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    PVR_SOURCE source,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  const std::string strUrl = GetChannelStreamURL(channel.GetUniqueId());
  kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", strUrl.c_str());
  PVR_ERROR ret = PVR_ERROR_FAILED;
  if (!strUrl.empty())
  {
    SetStreamProperties(properties, strUrl, true);
    ret = PVR_ERROR_NO_ERROR;
  }
  return ret;
}

std::string PlutotvData::GetSettingsUUID(const std::string& setting)
{
  std::string uuid = kodi::addon::GetSettingString(setting);
  if (uuid.empty())
  {
    uuid = Utils::CreateUUID();
    kodi::Log(ADDON_LOG_DEBUG, "uuid (generated): %s", uuid.c_str());
    kodi::addon::SetSettingString(setting, uuid);
  }
  return uuid;
}

int PlutotvData::GetSettingsStartChannel() const
{
  return kodi::addon::GetSettingInt("start_channelnum", 1);
}

bool PlutotvData::GetSettingsColoredChannelLogos() const
{
  return kodi::addon::GetSettingBoolean("colored_channel_logos", true);
}

bool PlutotvData::GetSettingsWorkaroundBrokenStreams() const
{
  return kodi::addon::GetSettingBoolean("workaround_broken_streams", true);
}

std::string PlutotvData::GetChannelStreamURL(int uniqueId)
{
  LoadChannelsData();
  if (!m_bChannelsLoaded)
    return {};

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == uniqueId)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Get live url for channel %s", channel.strChannelName.c_str());

      // Complete stream URL with JWT.
      const std::string streamURL{channel.strStreamURL + m_jwt};
      kodi::Log(ADDON_LOG_DEBUG, "stream URL: %s", streamURL.c_str());
      return streamURL;
    }
  }
  return {};
}

PVR_ERROR PlutotvData::GetChannelGroupsAmount(int& amount)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR PlutotvData::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR PlutotvData::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                              kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR PlutotvData::GetEPGForChannel(int channelUid,
                                        time_t start,
                                        time_t end,
                                        kodi::addon::PVREPGTagsResultSet& results)
{
  LoadChannelsData();
  if (!m_bChannelsLoaded)
    return PVR_ERROR_SERVER_ERROR;

  // Find channel data
  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId != channelUid)
      continue;

    // Channel data found
    if (!m_epg_cache_document || m_epg_cache_start == 0 || m_epg_cache_end == 0 ||
        start < m_epg_cache_start || end > m_epg_cache_end)
    {
      const time_t orig_start = start;
      const time_t now = std::time(nullptr);
      if (orig_start < now)
      {
        kodi::Log(ADDON_LOG_DEBUG, "[epg] adjusting start time to 'now' minus 3 hrs");
        start = now - 7200; // Pluto.tv API returns nothing if we step back (to wide) in time.
      }

      const std::string jsonEpg{GetEpgJson(start)};
      kodi::Log(ADDON_LOG_DEBUG, "[epg-all] %s", jsonEpg.c_str());
      if (jsonEpg.empty())
      {
        kodi::Log(ADDON_LOG_ERROR, "[epg] empty server response");
        return PVR_ERROR_SERVER_ERROR;
      }

      const auto epgDoc{std::make_shared<nlohmann::json>(nlohmann::json::parse(jsonEpg.c_str()))};
      if ((*epgDoc).is_discarded())
      {
        kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
        return PVR_ERROR_SERVER_ERROR;
      }

      m_epg_cache_document = epgDoc;
      m_epg_cache_start = orig_start;
      m_epg_cache_end = end;
    }

    kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");

    kodi::Log(ADDON_LOG_DEBUG, "[epg] size: %i;", (*m_epg_cache_document).at("data").size());

    // Find EPG data
    for (const auto& epgChannel : (*m_epg_cache_document).at("data"))
    {
      if (epgChannel.at("channelId") != channel.plutotvID)
        continue;

      // EPG data found
      for (const auto& epgData : epgChannel.at("timelines"))
      {
        kodi::addon::PVREPGTag tag;

        // generate a unique boadcast id
        const std::string epg_bsid = epgData.at("_id");
        kodi::Log(ADDON_LOG_DEBUG, "[epg] epg_bsid: %s;", epg_bsid.c_str());
        const int epg_bid = Utils::Hash(epg_bsid);
        kodi::Log(ADDON_LOG_DEBUG, "[epg] epg_bid: %i;", epg_bid);
        tag.SetUniqueBroadcastId(epg_bid);

        // channel ID
        tag.SetUniqueChannelId(channel.iUniqueId);

        // set title
        tag.SetTitle(epgData.at("title"));
        kodi::Log(ADDON_LOG_DEBUG, "[epg] title: %s;", nlohmann::to_string(epgData.at("title")).c_str());

        // startTime
        std::string startTime = epgData.at("start");
        tag.SetStartTime(Utils::StringToTime(startTime));

        // endTime
        std::string endTime = epgData.at("stop");
        tag.SetEndTime(Utils::StringToTime(endTime));

        if (epgData.contains("episode"))
        {
          const auto& episode = epgData.at("episode");
          // description
          if (episode.contains("description") && episode.at("description"))
          {
            tag.SetPlot(episode.at("description"));
            kodi::Log(ADDON_LOG_DEBUG, "[epg] episode description: %s;", nlohmann::to_string(episode.at("description")).c_str());
          }

          // genre
          if (episode.contains("genre") && episode.at("genre"))
          {
            tag.SetGenreType(EPG_GENRE_USE_STRING);
            tag.SetGenreDescription(episode.at("genre"));
            kodi::Log(ADDON_LOG_INFO, "[epg]] episode genre: %s", nlohmann::to_string(episode.at("genre")).c_str());
          }

          // thumbnail
          if (episode.contains("thumbnail") && episode.at("thumbnail").at("path"))
          {
            tag.SetIconPath(episode.at("thumbnail").at("path"));
            kodi::Log(ADDON_LOG_INFO, "[epg]] episode thumbnail: %s", nlohmann::to_string(episode.at("thumbnail").at("path")).c_str());
          }

          // first aired
          if (episode.contains("firstAired") && episode.at("firstAired"))
          {
            tag.SetFirstAired(episode.at("firstAired"));
            kodi::Log(ADDON_LOG_INFO, "[epg] episode first aired: %s", nlohmann::to_string(episode.at("firstAired")).c_str());
          }

          // parental rating (as age number,"FSK-*", "Not Rated")
          if (episode.contains("rating") && episode.at("rating"))
          {
            const std::string ratingString{episode.at("rating")};
            kodi::Log(ADDON_LOG_INFO, "[epg] episode rating: %s", ratingString.c_str());

            // rating as string
            tag.SetParentalRatingCode(ratingString);

            const int rating{Utils::StringToInt(ratingString, -1)};
            if (rating > -1)
            {
              // rating as age number
              tag.SetParentalRating(rating);
            }
          }

          // season number
          if (episode.contains("season") && episode.at("season"))
          {
            tag.SetSeriesNumber(episode.at("season"));
            kodi::Log(ADDON_LOG_DEBUG, "[epg] season number: %s;", nlohmann::to_string(episode.at("season")).c_str());
          }

          // episode number
          if (episode.contains("number") && episode.at("number"))
          {
            tag.SetEpisodeNumber(episode.at("number"));
            kodi::Log(ADDON_LOG_DEBUG, "[epg] episode number: %s;", nlohmann::to_string(episode.at("number")).c_str());
          }

          // series title / episode name
          if (episode.contains("series") && episode.at("series").contains("name") &&
              episode.at("series").at("name") && episode.contains("name") &&
              episode.at("name"))
          {
            // series title
            tag.SetTitle(episode.at("series").at("name"));
            kodi::Log(ADDON_LOG_DEBUG, "[epg] series title: %s;", nlohmann::to_string(episode.at("series").at("name")).c_str());

            // episode name
            tag.SetEpisodeName(episode.at("name"));
            kodi::Log(ADDON_LOG_DEBUG, "[epg] episode name: %s;", nlohmann::to_string(episode.at("name")).c_str());

            // set is series
            tag.SetFlags(EPG_TAG_FLAG_IS_SERIES);
          }
        }

        results.Add(tag);
      }
      return PVR_ERROR_NO_ERROR;
    }
    // EPG for channel not found. This is not an error. Channel might just have no EPG data.
    return PVR_ERROR_NO_ERROR;
  }

  kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: channel not found");
  return PVR_ERROR_INVALID_PARAMETERS;
}

std::string PlutotvData::GetJWT()
{
  // JWT expires after 24 hours
  if (m_jwt.empty() || (std::chrono::steady_clock::now() - m_jwtTimestamp > std::chrono::hours(23)))
  {
    std::string url{"https://boot.pluto.tv/v4/start"};
    url += "?appName=web";
    url += "&appVersion=1.0.0";
    url += "&deviceVersion=122.0.0"; // has to match user agent?
    url += "&deviceModel=web";
    url += "&deviceMake=chrome"; // has to match user agent?
    url += "&deviceType=web";
    url += "&clientID=" + GetSettingsUUID("internal_clientid");
    url += "&clientModelNumber=1.0.0";
    url += "&serverSideAds=false";
    url += "&drmCapabilities=widevine%3AL3"; // Widevine L3 device
    url += "&blockingMode=";
    url += "&notificationVersion=1";
    url += "&appLaunchCount=";
    url += "&lastAppLaunchDate=";

    m_jwt.clear();

    Curl curl;
    curl.AddHeader("User-Agent", PLUTOTV_USER_AGENT);

    int statusCode{500};
    const std::string json{curl.Get(url, statusCode)};
    if (statusCode == 200)
    {
      nlohmann::json doc = nlohmann::json::parse(json.c_str());
      if (doc.is_discarded())
      {
        kodi::Log(ADDON_LOG_ERROR, "[GetJWT] ERROR: error while parsing json");
      }
      else
      {
        m_jwt = doc.at("sessionToken");
        m_jwtTimestamp = std::chrono::steady_clock::now();
        kodi::Log(ADDON_LOG_DEBUG, "[GetJWT]: New JWT: %s.", m_jwt.c_str());
      }
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "[GetJWT] error. status: %i, body: %s", statusCode, json.c_str());
    }
  }
  return m_jwt;
}

std::string PlutotvData::GetChannelsJson() const
{
  std::string url{"https://service-channels.clusters.pluto.tv/v2/guide/channels"};
  url += "?channelIds="; // all channels
  url += "&offset=0";
  url += "&limit=1000";
  url += "&sort=number:asc";

  Curl curl;
  curl.AddHeader("authority", "service-channels.clusters.pluto.tv");
  curl.AddHeader("accept", "*/*");
  curl.AddHeader("accept-language", "en-US,en;q=0.9");
  curl.AddHeader("authorization", "Bearer " + m_jwt);
  curl.AddHeader("origin", "https://pluto.tv");
  curl.AddHeader("referer", "https://pluto.tv/");
  curl.AddHeader("user-agent", PLUTOTV_USER_AGENT);

  int statusCode{500};
  const std::string json{curl.Get(url, statusCode)};
  if (statusCode == 200)
  {
    kodi::Log(ADDON_LOG_DEBUG, "[GetChannelsJson] Response: %s.", json.c_str());
    return json;
  }

  kodi::Log(ADDON_LOG_ERROR, "[GetChannelsJson] ERROR. status: %i, body: %s", statusCode,
            json.c_str());
  return {};
}

std::string PlutotvData::GetEpgJson(time_t start) const
{
  const std::tm* pstm{std::localtime(&start)};
  char startTime[21] = {};
  std::strftime(startTime, sizeof(startTime), "%Y-%m-%dT%H:%M:%SZ", pstm);

  std::string url{"https://service-channels.clusters.pluto.tv/v2/guide/timelines"};
  url += "?start=" + std::string{startTime};
  url += "&channelIds="; // all channels
  url += "&duration=720"; // 12 hours

  Curl curl;
  curl.AddHeader("authority", "service-channels.clusters.pluto.tv");
  curl.AddHeader("accept", "*/*");
  curl.AddHeader("accept-language", "en-US,en;q=0.9");
  curl.AddHeader("authorization", "Bearer " + m_jwt);
  curl.AddHeader("origin", "https://pluto.tv");
  curl.AddHeader("referer", "https://pluto.tv/");
  curl.AddHeader("user-agent", PLUTOTV_USER_AGENT);

  int statusCode{500};
  const std::string json{curl.Get(url, statusCode)};
  if (statusCode == 200)
  {
    kodi::Log(ADDON_LOG_DEBUG, "[GetEpgJson] Response: %s.", json.c_str());
    return json;
  }

  kodi::Log(ADDON_LOG_ERROR, "[GetEpgJson] ERROR. status: %i, body: %s", statusCode, json.c_str());
  return "";
}

ADDONCREATOR(PlutotvData)
