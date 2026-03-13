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
  rapidjson::Document channelsDoc;
  channelsDoc.Parse(jsonChannels.c_str());
  if (channelsDoc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "[LoadChannelData] ERROR: error while parsing json");
    return false;
  }
  kodi::Log(ADDON_LOG_DEBUG, "[channels] iterate channels");
  kodi::Log(ADDON_LOG_DEBUG, "[channels] size: %i;", channelsDoc["data"].Size());

  // Use configured start channel number to populate the channel list
  int i = GetSettingsStartChannel();
  for (const auto& channel : channelsDoc["data"].GetArray())
  {
    const std::string plutotvid{channel["id"].GetString()};

    PlutotvChannel plutotv_channel;
    plutotv_channel.iChannelNumber = i++; // position
    kodi::Log(ADDON_LOG_DEBUG, "[channel] channelnr(pos): %i;", plutotv_channel.iChannelNumber);

    plutotv_channel.plutotvID = plutotvid;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] pluto.tv ID: %s;", plutotv_channel.plutotvID.c_str());

    const int uniqueId = Utils::Hash(plutotvid);
    plutotv_channel.iUniqueId = uniqueId;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] id: %i;", uniqueId);

    const std::string displayName = channel["name"].GetString();
    plutotv_channel.strChannelName = displayName;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] name: %s;", plutotv_channel.strChannelName.c_str());

    std::string logo;

    if (channel.HasMember("images") && channel["images"].Size() > 0)
    {
      for (const auto& l : channel["images"].GetArray())
      {
        if (!l.HasMember("type"))
          continue;

        if (GetSettingsColoredChannelLogos())
        {
          if (l["type"] == "colorLogoPNG")
          {
            logo = l["url"].GetString();
            break;
          }
        }
        else if (l["type"] == "solidLogoPNG")
        {
          logo = l["url"].GetString();
          break;
        }
        // fallback, should always work
        if (l["type"] == "logo")
        {
          logo = l["url"].GetString();
          // no break, since we might find the proper image with one of the next iterations
        }
      }
    }

    plutotv_channel.strIconPath = logo;
    kodi::Log(ADDON_LOG_DEBUG, "[channel] iconpath: %s;", plutotv_channel.strIconPath.c_str());

    if (channel.HasMember("stitched") && channel["stitched"].HasMember("paths") &&
        channel["stitched"]["paths"].Size() > 0)
    {
      std::string streamURL{"https://cfd-v4-service-channel-stitcher-use1-1.prd.pluto.tv/v2"};
      streamURL += channel["stitched"]["paths"][0]["path"].GetString();
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

      const auto epgDoc(std::make_shared<rapidjson::Document>());
      epgDoc->Parse(jsonEpg.c_str());
      if (epgDoc->GetParseError())
      {
        kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
        return PVR_ERROR_SERVER_ERROR;
      }

      m_epg_cache_document = epgDoc;
      m_epg_cache_start = orig_start;
      m_epg_cache_end = end;
    }

    kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");

    kodi::Log(ADDON_LOG_DEBUG, "[epg] size: %i;", (*m_epg_cache_document)["data"].Size());

    // Find EPG data
    for (const auto& epgChannel : (*m_epg_cache_document)["data"].GetArray())
    {
      if (epgChannel["channelId"].GetString() != channel.plutotvID)
        continue;

      // EPG data found
      for (const auto& epgData : epgChannel["timelines"].GetArray())
      {
        kodi::addon::PVREPGTag tag;

        // generate a unique boadcast id
        const std::string epg_bsid = epgData["_id"].GetString();
        kodi::Log(ADDON_LOG_DEBUG, "[epg] epg_bsid: %s;", epg_bsid.c_str());
        const int epg_bid = Utils::Hash(epg_bsid);
        kodi::Log(ADDON_LOG_DEBUG, "[epg] epg_bid: %i;", epg_bid);
        tag.SetUniqueBroadcastId(epg_bid);

        // channel ID
        tag.SetUniqueChannelId(channel.iUniqueId);

        // title
        tag.SetTitle(epgData["title"].GetString());
        kodi::Log(ADDON_LOG_DEBUG, "[epg] title: %s;", epgData["title"].GetString());

        // startTime
        std::string startTime = epgData["start"].GetString();
        tag.SetStartTime(Utils::StringToTime(startTime));

        // endTime
        std::string endTime = epgData["stop"].GetString();
        tag.SetEndTime(Utils::StringToTime(endTime));

        if (epgData.HasMember("episode"))
        {
          const auto& episode = epgData["episode"];

          // description
          if (episode.HasMember("description") && episode["description"].IsString())
          {
            tag.SetPlot(episode["description"].GetString());
            kodi::Log(ADDON_LOG_DEBUG, "[epg] episode description: %s;",
                      episode["description"].GetString());
          }

          // genre
          if (episode.HasMember("genre") && episode["genre"].IsString())
          {
            tag.SetGenreType(EPG_GENRE_USE_STRING);
            tag.SetGenreDescription(episode["genre"].GetString());
            kodi::Log(ADDON_LOG_INFO, "[epg]] episode genre: %s", episode["genre"].GetString());
          }

          // thumbnail
          if (episode.HasMember("thumbnail") && episode["thumbnail"]["path"].IsString())
          {
            tag.SetIconPath(episode["thumbnail"]["path"].GetString());
            kodi::Log(ADDON_LOG_INFO, "[epg]] episode thumbnail: %s",
                      episode["thumbnail"]["path"].GetString());
          }

          // first aired
          if (episode.HasMember("firstAired") && episode["firstAired"].IsString())
          {
            tag.SetFirstAired(episode["firstAired"].GetString());
            kodi::Log(ADDON_LOG_INFO, "[epg]] episode first aired: %s",
                      episode["firstAired"].GetString());
          }

          // parental rating (as age number,"FSK-*", "Not Rated")
          if (episode.HasMember("rating") && episode["rating"].IsString())
          {
            const std::string ratingString{episode["rating"].GetString()};
            kodi::Log(ADDON_LOG_INFO, "[epg]] episode rating: %s", ratingString.c_str());

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
          if (episode.HasMember("season") && episode["season"].IsInt())
          {
            tag.SetSeriesNumber(episode["season"].GetInt());
            kodi::Log(ADDON_LOG_DEBUG, "[epg] season number: %d;", episode["season"].GetInt());
          }

          // episode number
          if (episode.HasMember("number") && episode["number"].IsInt())
          {
            tag.SetEpisodeNumber(episode["number"].GetInt());
            kodi::Log(ADDON_LOG_DEBUG, "[epg] episode number: %d;", episode["number"].GetInt());
          }

          // series title / episode name
          if (episode.HasMember("series") && episode["series"].HasMember("name") &&
              episode["series"]["name"].IsString() && episode.HasMember("name") &&
              episode["name"].IsString())
          {
            // series title
            tag.SetTitle(episode["series"]["name"].GetString());
            kodi::Log(ADDON_LOG_DEBUG, "[epg] series title: %s;",
                      episode["series"]["name"].GetString());

            // episode name
            tag.SetEpisodeName(episode["name"].GetString());
            kodi::Log(ADDON_LOG_DEBUG, "[epg] episode name: %s;", episode["name"].GetString());

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
      rapidjson::Document doc;
      doc.Parse(json.c_str());
      if (doc.GetParseError())
      {
        kodi::Log(ADDON_LOG_ERROR, "[GetJWT] ERROR: error while parsing json");
      }
      else
      {
        m_jwt = doc["sessionToken"].GetString();
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
