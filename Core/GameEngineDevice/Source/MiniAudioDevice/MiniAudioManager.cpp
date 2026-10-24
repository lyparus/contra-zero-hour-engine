/*
**	Command & Conquer Generals(tm)
**	Copyright 2026 Stephan Vedder
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  (c) 2001-2003 Electronic Arts Inc.                                       //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// FILE: MiniAudioManager.cpp
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  MiniAudioManager.cpp                                          */
/* Created:    Stephan Vedder (feliwir), June 2026                           */
/*             Adapted for GeneralsX Core architecture                       */
/* Desc:       Implementation for the MiniAudioManager, interfacing with    */
/*             the miniaudio Sound System.                                   */
/* Reference:  https://github.com/feliwir/CnC_Generals_Zero_Hour            */
/*---------------------------------------------------------------------------*/

#include "Lib/BaseType.h"
#define MINIAUDIO_IMPLEMENTATION
#include "MiniAudioDevice/MiniAudioManager.h"
#include "MiniAudioDevice/MiniAudioStream.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/AsciiString.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"

#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"
// GeneralsX @bugfix fbraz3 24/06/2026 Save and restore FPU precision mode when calling audio manager entrypoints.
#include "GameLogic/FPUControl.h"

#include "Common/file.h"

#ifdef _INTERNAL
//#pragma optimize("", off)
#endif

enum { INFINITE_LOOP_COUNT = 1000000 };

//-------------------------------------------------------------------------------------------------
MiniAudioManager::MiniAudioManager() :
	m_playbackDevices(NULL),
	m_playbackDeviceCount(0),
	m_deviceOpened(false),
	m_selectedPlaybackDevice(PROVIDER_ERROR),
	m_selectedSpeakerType(0),
	m_lastSelectedPlaybackDevice(PROVIDER_ERROR),
	m_binkHandle(NULL),
	m_pref3DProvider(AsciiString::TheEmptyString),
	m_prefSpeaker(AsciiString::TheEmptyString)
{
}

//-------------------------------------------------------------------------------------------------
MiniAudioManager::~MiniAudioManager()
{
	DEBUG_ASSERTCRASH(m_binkHandle == NULL, ("Leaked a Bink handle. Chuybregts"));
	releaseHandleForBink();
	closeDevice();

	DEBUG_ASSERTCRASH(this == TheAudio, ("Umm...\n"));
	TheAudio = NULL;
}

//-------------------------------------------------------------------------------------------------
#if defined(RTS_DEBUG)
AudioHandle MiniAudioManager::addAudioEvent(const AudioEventRTS *eventToAdd)
{
	ScopedFPUGuard fpuGuard;
	if (TheGlobalData->m_preloadReport) {
		if (!eventToAdd->getEventName().isEmpty()) {
			m_allEventsLoaded.insert(eventToAdd->getEventName());
		}
	}

	return AudioManager::addAudioEvent(eventToAdd);
}
#endif

#if defined(RTS_DEBUG)
//-------------------------------------------------------------------------------------------------
void MiniAudioManager::audioDebugDisplay(DebugDisplayInterface *dd, void *, FILE *fp)
{
	std::list<MiniPlayingAudio *>::iterator it;

	Coord3D lookPos;
	TheTacticalView->getPosition(&lookPos);
	lookPos.z = TheTerrainLogic->getGroundHeight(lookPos.x, lookPos.y);
	const Coord3D *mikePos = TheAudio->getListenerPosition();
	Coord3D distanceVector = TheTacticalView->get3DCameraPosition();
	distanceVector.sub(mikePos);

	if (dd)
	{
		dd->printf("MiniAudio version: %s    ", ma_version_string());
		dd->printf("Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No"));
		dd->printf("3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No"));
		dd->printf("Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No"));
		dd->printf("Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No"));
		dd->printf("Channels Available: ");
		dd->printf("%d Sounds    ", m_sound->getAvailableSamples());
		dd->printf("Volume: ");
		dd->printf("Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f));
		dd->printf("3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f));
		dd->printf("Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f));
		dd->printf("Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f));
		dd->printf("Current 3D Provider: %s    ", TheAudio->getProviderName(getSelectedProvider()).str());
		dd->printf("Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());
		dd->printf("Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n",
			(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z);
		dd->printf("Camera distance from microphone: %d -- Zoom Volume: %d%%\n",
			(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume() * 100.0f));
		dd->printf("-----------------------------------------------------------\n");
		dd->printf("Playing Audio:\n");

		int i = 0;
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			MiniPlayingAudio *playing = *it;
			if (!playing || !playing->m_audioEventRTS) continue;
			AsciiString fn = playing->m_audioEventRTS->getFilename();
			fn = fn.reverseFind('\\') + 1;
			Real volume = 100.0f * getEffectiveVolume(playing->m_audioEventRTS);
			dd->printf("%2d: %-20s - (%s) Volume: %d\n", i++,
				playing->m_audioEventRTS->getEventName().str(), fn.str(), REAL_TO_INT(volume));
		}
	}

	if (fp)
	{
		fprintf(fp, "MiniAudio version: %s\n", ma_version_string());
	}
}
#endif

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::init()
{
	ScopedFPUGuard fpuGuard;
	AudioManager::init();
#ifdef INTENSE_DEBUG
	return;
#endif
	openDevice();
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::reset()
{
#if defined(RTS_DEBUG)
	dumpAllAssetsUsed();
	m_allEventsLoaded.clear();
#endif

	AudioManager::reset();
	stopAllAudioImmediately();
	// GeneralsX @bugfix coolswood 18/07/2026 Restart sound groups in reset() to fix silent audio after loadGame.
	// stopAllAudioImmediately() releases individual MiniPlayingAudio entries but does not touch the four
	// ma_sound_group nodes. If a prior pauseAudio() had stopped them (e.g. via the ESC menu used to pick
	// "Load"), they remain stopped across loadGame(). New sounds attach to the stopped groups via
	// ma_sound_init_from_data_source() and are then silently reaped by processPlayingList() because
	// ma_sound_is_playing() returns false — the user observes "no sound until ESC toggled", since the
	// pause/unpause cycle finally calls resumeAudio() -> ma_sound_group_start(). Restart the groups here
	// so audio works immediately after a save load. Guarded by the per-type *On flags: if the device
	// never opened (openDevice bailed out), the groups were never ma_sound_group_init'd, so touching
	// them would be unsafe.
	if (m_deviceOpened) {
		resumeAudio(AudioAffect_All);
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::update()
{
	ScopedFPUGuard fpuGuard;

	AudioManager::update();
	setDeviceListenerPosition();
	processRequestList();
	processPlayingList();
	processFadingList();
	processStoppedList();
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::stopAudio(AudioAffect which)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	if (!m_deviceOpened)
		return;
	if (BitIsSet(which, AudioAffect_Sound))
		ma_sound_group_stop(&m_soundGroup);
	if (BitIsSet(which, AudioAffect_Sound3D))
		ma_sound_group_stop(&m_sound3DGroup);
	if (BitIsSet(which, AudioAffect_Speech))
		ma_sound_group_stop(&m_speechGroup);
	if (BitIsSet(which, AudioAffect_Music))
		ma_sound_group_stop(&m_musicGroup);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::pauseAudio(AudioAffect which)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	if (!m_deviceOpened)
		return;
	if (BitIsSet(which, AudioAffect_Sound))
		ma_sound_group_stop(&m_soundGroup);
	if (BitIsSet(which, AudioAffect_Sound3D))
		ma_sound_group_stop(&m_sound3DGroup);
	if (BitIsSet(which, AudioAffect_Speech))
		ma_sound_group_stop(&m_speechGroup);
	if (BitIsSet(which, AudioAffect_Music))
		ma_sound_group_stop(&m_musicGroup);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::resumeAudio(AudioAffect which)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	if (!m_deviceOpened)
		return;
	if (BitIsSet(which, AudioAffect_Sound))
		ma_sound_group_start(&m_soundGroup);
	if (BitIsSet(which, AudioAffect_Sound3D))
		ma_sound_group_start(&m_sound3DGroup);
	if (BitIsSet(which, AudioAffect_Speech))
		ma_sound_group_start(&m_speechGroup);
	if (BitIsSet(which, AudioAffect_Music))
		ma_sound_group_start(&m_musicGroup);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::pauseAmbient(Bool shouldPause)
{
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::stopAllAmbientsBy(Object *obj)
{
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::stopAllAmbientsBy(Drawable *draw)
{
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::playAudioEvent(AudioRequest *req)
{
	AudioEventRTS *event = req->m_pendingEvent.Peek();
	if (!event) {
		return;
	}

	const AudioEventInfo *info = event->getAudioEventInfo();
	if (!info) {
		return;
	}

	if (!m_deviceOpened) {
		return;
	}

	AsciiString fileToPlay = event->getFilename();

	std::list<MiniPlayingAudio *>::iterator it;

	AudioHandle handleToKill = event->getHandleToKill();
	MiniPlayingAudio *audio = allocatePlayingAudio();
	audio->m_audioEventRTS = req->m_pendingEvent;

	// Kill existing sound if handleToKill is set
	if (handleToKill) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			MiniPlayingAudio *playing = (*it);
			if (!playing) continue;
			if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill)
			{
				releasePlayingAudio(playing);
				m_playingSounds.erase(it);
				break;
			}
		}
	}

	ma_sound_group *groupToUse = NULL;
	ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
	switch (info->m_soundType)
	{
	case AT_Music:
		groupToUse = &m_musicGroup;
		break;
	case AT_Streaming:
		groupToUse = &m_speechGroup;
		break;
	case AT_SoundEffect:
		if (event->isPositionalAudio()) {
			groupToUse = &m_sound3DGroup;
			flags = 0; // Allow spatialization for 3D
		}
		else {
			groupToUse = &m_soundGroup;
		}
		break;
	}

	if (!initSoundFromFile(audio, fileToPlay, flags, groupToUse)) {
		releasePlayingAudio(audio);
		return;
	}
	ma_sound *sound = audio->m_sound;

	switch (info->m_soundType)
	{
	case AT_Music:
	case AT_Streaming:
	{
		if ((info->m_soundType == AT_Streaming) && event->getUninterruptible()) {
			stopAllSpeech();
		}
		audio->m_type = MPAT_Stream;
		if ((info->m_soundType == AT_Streaming) && event->getUninterruptible()) {
			setDisallowSpeech(TRUE);
		}
		break;
	}

	case AT_SoundEffect:
	{
		if (event->isPositionalAudio()) {
			audio->m_type = MPAT_3DSample;
			if (event->getAudioEventInfo()->m_type & ST_GLOBAL) {
				ma_sound_set_min_distance(sound, TheAudio->getAudioSettings()->m_globalMinRange);
				ma_sound_set_max_distance(sound, TheAudio->getAudioSettings()->m_globalMaxRange);
			}
			else {
				ma_sound_set_min_distance(sound, event->getAudioEventInfo()->m_minDistance);
				ma_sound_set_max_distance(sound, event->getAudioEventInfo()->m_maxDistance);
			}
			const Coord3D *pos = event->getCurrentPosition();
			if (pos) {
				ma_sound_set_position(sound, pos->x, pos->y, pos->z);
			}

		}
		else {
			audio->m_type = MPAT_Sample;

		}
		break;
	}
	}

	// GeneralsX @bugfix Mr. Meeseeks 19/06/2026 - Initialize volume using adjustPlayingVolume
	adjustPlayingVolume(audio);

	const ma_result result = ma_sound_start(sound);
	if (result != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Failed to start sound: %d for '%s'", result, fileToPlay.str()));
		releasePlayingAudio(audio);
		return;
	}

	m_playingSounds.push_back(audio);
}

//-------------------------------------------------------------------------------------------------
// Contra: reads the whole sound file through TheFileSystem (works for .big archives) and lets a
// miniaudio decoder stream from that memory while the sound plays. Replaces the FFmpeg decoding of
// GeneralsX; the game uses PCM / IMA ADPCM .wav and .mp3, which miniaudio decodes itself.
Bool MiniAudioManager::initSoundFromFile(MiniPlayingAudio *audio, const AsciiString &fileName, ma_uint32 flags, ma_sound_group *group)
{
	if (fileName.isEmpty()) {
		return false;	// events without a sound file are silent, as with Miles
	}

	File *file = TheFileSystem->openFile(fileName.str(), File::READ | File::BINARY);
	if (!file) {
		DEBUG_LOG(("MiniAudio: Failed to open file: %s", fileName.str()));
		return false;
	}

	const Int size = file->size();
	void *data = size > 0 ? malloc(size) : NULL;
	const Bool readOk = data && file->read(data, size) == size;
	file->close();
	if (!readOk) {
		DEBUG_LOG(("MiniAudio: Failed to read file: %s", fileName.str()));
		free(data);
		return false;
	}

	ma_decoder *decoder = (ma_decoder *)malloc(sizeof(ma_decoder));
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_memory(data, (size_t)size, &decoderConfig, decoder) != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Unsupported audio format: %s", fileName.str()));
		free(decoder);
		free(data);
		return false;
	}

	ma_sound *sound = (ma_sound *)malloc(sizeof(ma_sound));
	const ma_result result = ma_sound_init_from_data_source(&m_engine, decoder, flags, group, sound);
	if (result != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Failed to init sound: %d for '%s'", result, fileName.str()));
		free(sound);
		ma_decoder_uninit(decoder);
		free(decoder);
		free(data);
		return false;
	}

	audio->m_sound = sound;
	audio->m_decoder = decoder;
	audio->m_fileData = data;
	return true;
}

//-------------------------------------------------------------------------------------------------
// Contra: a sound reached its end. Miles reported this through a callback (notifyOfAudioCompletion);
// here processPlayingList polls it. Returns true when the sound goes on (next loop started).
Bool MiniAudioManager::handleFinishedSound(MiniPlayingAudio *playing)
{
	if (!playing->m_audioEventRTS || !playing->m_audioEventRTS->getAudioEventInfo())
		return false;

	if (getDisallowSpeech() && playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Streaming)
		setDisallowSpeech(FALSE);

	if (playing->m_audioEventRTS->getAudioEventInfo()->m_control & AC_LOOP) {
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Attack)
			playing->m_audioEventRTS->setNextPlayPortion(PP_Sound);
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Sound) {
			playing->m_audioEventRTS->decreaseLoopCount();
			return startNextLoop(playing);
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::stopAudioEvent(AudioHandle handle)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	std::list<MiniPlayingAudio *>::iterator it;

	// Handle special music stop commands
	if (handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			MiniPlayingAudio *audio = (*it);
			if (!audio || !audio->m_audioEventRTS) continue;

			const AudioEventInfo *stopInfo = audio->m_audioEventRTS->getAudioEventInfo();
			if (stopInfo && stopInfo->m_soundType == AT_Music)
			{
				if (handle == AHSV_StopTheMusicFade)
					m_fadingAudio.push_back(audio);
				else
					releasePlayingAudio(audio);
				m_playingSounds.erase(it);
				break;
			}
		}
	}

	// Find and mark the audio event for stopping
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *audio = (*it);
		if (!audio || !audio->m_audioEventRTS) continue;
		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			audio->m_requestStop = true;
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::killAudioEventImmediately(AudioHandle audioEvent)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	// First look for it in the request list.
	std::list<AudioRequest *>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ait++)
	{
		AudioRequest *req = (*ait);
		if (req && req->m_request == AR_Play && req->m_handleToInteractOn == audioEvent)
		{
			deleteInstance(req);
			ait = m_audioRequests.erase(ait);
			return;
		}
	}

	std::list<MiniPlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); it++)
	{
		MiniPlayingAudio *audio = (*it);
		if (!audio) continue;
		if (audio->m_audioEventRTS && audio->m_audioEventRTS->getPlayingHandle() == audioEvent)
		{
			releasePlayingAudio(audio);
			m_playingSounds.erase(it);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::pauseAudioEvent(AudioHandle handle)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	// TODO: implement individual sound pause
}

//-------------------------------------------------------------------------------------------------
MiniPlayingAudio *MiniAudioManager::allocatePlayingAudio(void)
{
	MiniPlayingAudio *aud = NEW MiniPlayingAudio;
	return aud;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::releaseMiniAudioHandles(MiniPlayingAudio *release)
{
	// Uninit sound BEFORE audio buffer — sound holds reference to buffer
	if (release->m_sound) {
		ma_sound_uninit(release->m_sound);
		free(release->m_sound);
		release->m_sound = NULL;
	}
	if (release->m_decoder) {
		ma_decoder_uninit(release->m_decoder);
		free(release->m_decoder);
		release->m_decoder = NULL;
	}
	free(release->m_fileData);
	release->m_fileData = NULL;
	release->m_type = MPAT_INVALID;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::releasePlayingAudio(MiniPlayingAudio *release)
{
	if (!release) return;

	const AudioEventInfo *releaseInfo = release->m_audioEventRTS
		? release->m_audioEventRTS->getAudioEventInfo() : nullptr;

	if (releaseInfo && releaseInfo->m_soundType == AT_SoundEffect && release->m_sound) {
		if (release->m_type == MPAT_Sample) {

		}
		else if (release->m_type == MPAT_3DSample) {

		}
		// MPAT_Stream and MPAT_INVALID don't notify
	}

	releaseMiniAudioHandles(release);
	delete release;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::stopAllAudioImmediately(void)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	std::list<MiniPlayingAudio *>::iterator it;
	MiniPlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		playing = *it;
		it = m_playingSounds.erase(it); // advance before release
		if (playing) releasePlayingAudio(playing);
	}

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ) {
		playing = *it;
		it = m_fadingAudio.erase(it);
		if (playing) releasePlayingAudio(playing);
	}

	for (it = m_stoppedAudio.begin(); it != m_stoppedAudio.end(); ) {
		playing = *it;
		it = m_stoppedAudio.erase(it);
		if (playing) releasePlayingAudio(playing);
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::freeAllMiniAudioHandles(void)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	if (m_deviceOpened)
		ma_engine_stop(&m_engine);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::adjustPlayingVolume(MiniPlayingAudio *audio)
{
	if (!audio || !audio->m_audioEventRTS || !audio->m_sound) return;

	Real desiredVolume = audio->m_audioEventRTS->getVolume() * audio->m_audioEventRTS->getVolumeShift();
	if (audio->m_type == MPAT_Sample) {
		ma_sound_set_volume(audio->m_sound, m_soundVolume * desiredVolume);
	}
	else if (audio->m_type == MPAT_3DSample) {
		ma_sound_set_volume(audio->m_sound, m_sound3DVolume * desiredVolume);
	}
	else if (audio->m_type == MPAT_Stream) {
		const AudioEventInfo *info = audio->m_audioEventRTS->getAudioEventInfo();
		if (info && info->m_soundType == AT_Music)
			ma_sound_set_volume(audio->m_sound, m_musicVolume * desiredVolume);
		else
			ma_sound_set_volume(audio->m_sound, m_speechVolume * desiredVolume);
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::stopAllSpeech(void)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	std::list<MiniPlayingAudio *>::iterator it;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		MiniPlayingAudio *playing = (*it);
		if (!playing) { ++it; continue; }

		if (playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo() &&
			playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Streaming) {
			it = m_playingSounds.erase(it);
			releasePlayingAudio(playing);
		}
		else {
			++it;
		}
	}
}

//-------------------------------------------------------------------------------------------------
AsciiString MiniAudioManager::nextMusicTrack(void)
{
	AsciiString trackName;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo() &&
			playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);
	trackName = nextTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
	return trackName;
}

//-------------------------------------------------------------------------------------------------
AsciiString MiniAudioManager::prevMusicTrack(void)
{
	AsciiString trackName;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo() &&
			playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);
	trackName = prevTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
	return trackName;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isMusicPlaying(void) const
{
	return m_deviceOpened && ma_sound_group_is_playing(&m_musicGroup) == MA_TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::hasMusicTrackCompleted(const AsciiString &trackName, Int numberOfTimes) const
{
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
AsciiString MiniAudioManager::getMusicTrackName(void) const
{
	for (auto ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		if ((*ait)->m_request != AR_Play || !(*ait)->m_pendingEvent) continue;
		if ((*ait)->m_pendingEvent->getAudioEventInfo()->m_soundType == AT_Music)
			return (*ait)->m_pendingEvent->getEventName();
	}
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo() &&
			playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
			return playing->m_audioEventRTS->getEventName();
		}
	}
	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::openDevice(void)
{
	if (!TheGlobalData->m_audioOn || m_deviceOpened) {
		return;
	}

	ma_result result = ma_log_init(NULL, &m_log);
	if (result != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Failed to initialize log: %d", result));
		setOn(false, AudioAffect_All);
		return;
	}

	auto on_log = [](void *pUserData, ma_uint32 logLevel, const char *message) {
		if (logLevel <= MA_LOG_LEVEL_WARNING)
			DEBUG_LOG(("miniaudio [%s]: %s", ma_log_level_to_string(logLevel), message));
	};
	ma_log_register_callback(&m_log, ma_log_callback_init(on_log, NULL));

	ma_context_config contextConfig = ma_context_config_init();
	contextConfig.pLog = &m_log;

	result = ma_context_init(NULL, 0, &contextConfig, &m_context);
	if (result != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Failed to initialize context: %d", result));
		ma_log_uninit(&m_log);
		setOn(false, AudioAffect_All);
		return;
	}

	result = ma_context_get_devices(&m_context, &m_playbackDevices, &m_playbackDeviceCount, NULL, NULL);
	if (result != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Failed to enumerate devices: %d", result));
		m_playbackDevices = NULL;
		m_playbackDeviceCount = 0;
	}

	ma_engine_config engineConfig = ma_engine_config_init();
	engineConfig.pContext = &m_context;
	engineConfig.pLog = &m_log;

	result = ma_engine_init(&engineConfig, &m_engine);
	if (result != MA_SUCCESS) {
		DEBUG_LOG(("MiniAudio: Failed to initialize engine: %d", result));
		ma_context_uninit(&m_context);
		ma_log_uninit(&m_log);
		setOn(false, AudioAffect_All);
		return;
	}

	ma_sound_group_init(&m_engine, 0, NULL, &m_musicGroup);
	ma_sound_group_init(&m_engine, 0, NULL, &m_soundGroup);
	ma_sound_group_init(&m_engine, 0, NULL, &m_sound3DGroup);
	ma_sound_group_init(&m_engine, 0, NULL, &m_speechGroup);
	// The game world is Z-up (miniaudio defaults to Y-up)
	ma_engine_listener_set_world_up(&m_engine, 0, 0.0f, 0.0f, 1.0f);
	m_deviceOpened = true;

	DEBUG_LOG(("MiniAudio %s: device '%s', %u playback devices, %u Hz",
		ma_version_string(),
		m_playbackDeviceCount > 0 ? m_playbackDevices[0].name : "default",
		m_playbackDeviceCount, ma_engine_get_sample_rate(&m_engine)));

	TheAudio->refreshCachedVariables();
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::closeDevice(void)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	// Stop all audio first to prevent use-after-free in audio callbacks
	stopAllAudioImmediately();

	if (!m_deviceOpened)
		return;
	m_deviceOpened = false;

	ma_sound_group_uninit(&m_speechGroup);
	ma_sound_group_uninit(&m_sound3DGroup);
	ma_sound_group_uninit(&m_soundGroup);
	ma_sound_group_uninit(&m_musicGroup);
	ma_engine_uninit(&m_engine);
	ma_context_uninit(&m_context);
	ma_log_uninit(&m_log);
	m_playbackDevices = NULL;
	m_playbackDeviceCount = 0;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isCurrentlyPlaying(AudioHandle handle)
{
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handle)
			return true;
	}
	for (auto ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		AudioRequest *req = *ait;
		if (req && req->m_pendingEvent && req->m_pendingEvent->getPlayingHandle() == handle)
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::notifyOfAudioCompletion(UnsignedInt audioCompleted, UnsignedInt flags)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	// Contra: Miles called this from its completion callback with a 32-bit sample handle. miniaudio has
	// no such callback; processPlayingList polls the sounds and calls handleFinishedSound instead.
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getProviderCount(void) const
{
	return m_playbackDeviceCount;
}

//-------------------------------------------------------------------------------------------------
AsciiString MiniAudioManager::getProviderName(UnsignedInt providerNum) const
{
	if (isOn(AudioAffect_Sound3D) && providerNum < m_playbackDeviceCount)
		return m_playbackDevices[providerNum].name;
	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getProviderIndex(AsciiString providerName) const
{
	for (UnsignedInt i = 0; i < m_playbackDeviceCount; ++i) {
		if (providerName == m_playbackDevices[i].name)
			return i;
	}
	return PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::selectProvider(UnsignedInt providerNdx) {}
void MiniAudioManager::unselectProvider(void) {}

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getSelectedProvider(void) const
{
	return m_selectedPlaybackDevice;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::setSpeakerType(UnsignedInt speakerType) {}

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getSpeakerType(void)
{
	return 0;
}

//-------------------------------------------------------------------------------------------------
void *MiniAudioManager::getHandleForBink(void)
{
	// Contra: without an opened device there is no engine to play the movie sound through
	if (!m_deviceOpened) {
		return NULL;
	}
	if (!m_binkHandle) {
		m_binkHandle = NEW MiniAudioStream;
		// Give the stream access to the engine
		static_cast<MiniAudioStream *>(m_binkHandle)->setEngine(&m_engine);
	}
	return m_binkHandle;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::releaseHandleForBink(void)
{
	if (m_binkHandle) {
		delete (MiniAudioStream *)m_binkHandle;
		m_binkHandle = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getNum2DSamples(void) const { return 0; }
UnsignedInt MiniAudioManager::getNum3DSamples(void) const { return 0; }
UnsignedInt MiniAudioManager::getNumStreams(void) const { return 0; }

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::doesViolateLimit(AudioEventRTS *event) const
{
	Int limit = event->getAudioEventInfo()->m_limit;
	if (limit == 0) return false;

	Int totalCount = 0;
	Int totalRequestCount = 0;

	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (!(*it)->m_audioEventRTS) continue;
		if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
			if (totalCount == 0)
				event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
			++totalCount;
		}
	}
	for (auto arIt = m_audioRequests.begin(); arIt != m_audioRequests.end(); ++arIt) {
		AudioRequest *req = (*arIt);
		if (req && req->m_pendingEvent &&
			req->m_pendingEvent->getEventName() == event->getEventName()) {
			totalRequestCount++;
			totalCount++;
		}
	}

	if (event->getAudioEventInfo()->m_control & AC_INTERRUPT) {
		if (totalRequestCount < limit) {
			Int totalPlayingCount = totalCount - totalRequestCount;
			if (totalRequestCount + totalPlayingCount < limit) {
				event->setHandleToKill(0);
				return false;
			}
			return false;
		}
	}

	if (totalCount < limit) {
		event->setHandleToKill(0);
		return false;
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isPlayingAlready(AudioEventRTS *event) const
{
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (!(*it)->m_audioEventRTS) continue;
		if ((*it)->m_audioEventRTS->getEventName() == event->getEventName())
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isObjectPlayingVoice(UnsignedInt objID) const
{
	if (objID == 0) return false;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (!(*it)->m_audioEventRTS) continue;
		const AudioEventInfo *info = (*it)->m_audioEventRTS->getAudioEventInfo();
		if (info && (*it)->m_audioEventRTS->getObjectID() == objID && (info->m_type & ST_VOICE))
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
AudioEventRTS *MiniAudioManager::findLowestPrioritySound(AudioEventRTS *event)
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if (priority == AP_LOWEST) return NULL;

	AudioEventRTS *lowestPriorityEvent = NULL;
	AudioPriority lowestPriority = AP_LOWEST;

	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		AudioEventRTS *itEvent = (*it)->m_audioEventRTS.Peek();
		if (!itEvent) continue;
		const AudioEventInfo *itInfo = itEvent->getAudioEventInfo();
		if (!itInfo) continue;
		AudioPriority itPriority = itInfo->m_priority;
		if (itPriority < priority) {
			if (!lowestPriorityEvent || lowestPriority > itPriority) {
				lowestPriorityEvent = itEvent;
				lowestPriority = itPriority;
				if (lowestPriority == AP_LOWEST) return lowestPriorityEvent;
			}
		}
	}
	return lowestPriorityEvent;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isPlayingLowerPriority(AudioEventRTS *event) const
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if (priority == AP_LOWEST) return false;

	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (!(*it)->m_audioEventRTS) continue;
		const AudioEventInfo *info = (*it)->m_audioEventRTS->getAudioEventInfo();
		if (info && info->m_priority < priority) {
			event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::killLowestPrioritySoundImmediately(AudioEventRTS *event)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	AudioEventRTS *lowestPriorityEvent = findLowestPrioritySound(event);
	if (!lowestPriorityEvent) return FALSE;

	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *playing = (*it);
		if (!playing) continue;
		if (playing->m_audioEventRTS && playing->m_audioEventRTS.Peek() == lowestPriorityEvent) {
			releasePlayingAudio(playing);
			m_playingSounds.erase(it);
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName) {
			playing->m_audioEventRTS->setVolume(newVolume);
			adjustPlayingVolume(playing);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::removePlayingAudio(AsciiString eventName)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName) {
			it = m_playingSounds.erase(it);
			releasePlayingAudio(playing);
		}
		else { ++it; }
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::removeAllDisabledAudio(void)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		MiniPlayingAudio *playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getVolume() == 0.0f) {
			it = m_playingSounds.erase(it);
			releasePlayingAudio(playing);
		}
		else { ++it; }
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::processRequestList(void)
{
	for (auto it = m_audioRequests.begin(); it != m_audioRequests.end(); ) {
		AudioRequest *req = (*it);
		if (req == NULL) { it = m_audioRequests.erase(it); continue; }

		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		if (!req->m_requiresCheckForSample || checkForSample(req))
			processRequest(req);

		deleteInstance(req);
		it = m_audioRequests.erase(it);
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::processPlayingList(void)
{
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		MiniPlayingAudio *playing = (*it);
		if (!playing) {
			it = m_playingSounds.erase(it);
			continue;
		}

		// Check if sound was explicitly marked to stop
		if (playing->m_requestStop) {
			if (playing->m_sound) ma_sound_stop(playing->m_sound);
			it = m_playingSounds.erase(it);
			releasePlayingAudio(playing);
			continue;
		}

		// Check if sound has finished playing (looping sounds start their next loop)
		if (playing->m_sound && !ma_sound_is_playing(playing->m_sound)) {
			if (handleFinishedSound(playing) && playing->m_sound && ma_sound_is_playing(playing->m_sound)) {
				++it;
				continue;
			}
			it = m_playingSounds.erase(it);
			releasePlayingAudio(playing);
			continue;
		}

		if (m_volumeHasChanged) {
			adjustPlayingVolume(playing);
		}

		// Update 3D position for positional sounds
		if (playing->m_type == MPAT_3DSample && playing->m_audioEventRTS) {
			const Coord3D *pos = getCurrentPositionFromEvent(playing->m_audioEventRTS.Peek());
			if (pos && playing->m_sound) {
				ma_sound_set_position(playing->m_sound, pos->x, pos->y, pos->z);
			}
		}

		++it;
	}

	if (m_volumeHasChanged) {
		m_volumeHasChanged = false;

		// GeneralsX @bugfix Push speech volume changes because movie audio bypasses the audio mixer
		if (TheVideoPlayer) {
			TheVideoPlayer->setVolume(getVolume(AudioAffect_Speech));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::processFadingList(void)
{
	for (auto it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ) {
		MiniPlayingAudio *playing = *it;
		if (!playing) {
			it = m_fadingAudio.erase(it);
			continue;
		}

		if (playing->m_framesFaded >= getAudioSettings()->m_fadeAudioFrames) {
			playing->m_requestStop = true;
			it = m_fadingAudio.erase(it);
			releasePlayingAudio(playing);
			continue;
		}

		++playing->m_framesFaded;
		Real volume = getEffectiveVolume(playing->m_audioEventRTS.Peek());
		volume *= (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames);

		if (playing->m_sound)
			ma_sound_set_volume(playing->m_sound, volume);

		++it;
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::processStoppedList(void)
{
	for (auto it = m_stoppedAudio.begin(); it != m_stoppedAudio.end(); ) {
		MiniPlayingAudio *playing = *it;
		it = m_stoppedAudio.erase(it);
		if (playing) releasePlayingAudio(playing);
	}
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::shouldProcessRequestThisFrame(AudioRequest *req) const
{
	if (!req->m_pendingEvent) return true;
	if (req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL) return true;
	return false;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::adjustRequest(AudioRequest *req)
{
	if (!req->m_pendingEvent) return;
	req->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = true;
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::checkForSample(AudioRequest *req)
{
	if (!req->m_pendingEvent) return true;
	if (req->m_pendingEvent->getAudioEventInfo() == NULL)
		getInfoForAudioEvent(req->m_pendingEvent.Peek());
	if (req->m_pendingEvent->getAudioEventInfo()->m_type != AT_SoundEffect)
		return true;
	return m_sound->canPlayNow(req->m_pendingEvent.Peek());
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::setHardwareAccelerated(Bool accel)
{
	AudioManager::setHardwareAccelerated(accel);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::setSpeakerSurround(Bool surround)
{
	AudioManager::setSpeakerSurround(surround);
}

//-------------------------------------------------------------------------------------------------
Real MiniAudioManager::getFileLengthMS(AsciiString strToLoad) const
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	if (strToLoad.isEmpty()) return 0.0f;

	File *file = TheFileSystem->openFile(strToLoad.str(), File::READ | File::BINARY);
	if (!file) return 0.0f;
	const Int size = file->size();
	void *data = size > 0 ? malloc(size) : NULL;
	const Bool readOk = data && file->read(data, size) == size;
	file->close();
	if (!readOk) {
		free(data);
		return 0.0f;
	}

	ma_decoder decoder;
	ma_decoder_config config = ma_decoder_config_init_default();
	ma_result result = ma_decoder_init_memory(data, (size_t)size, &config, &decoder);
	if (result != MA_SUCCESS) {
		free(data);
		return 0.0f;
	}

	ma_uint64 lengthInFrames;
	result = ma_decoder_get_length_in_pcm_frames(&decoder, &lengthInFrames);
	ma_uint32 sampleRate = decoder.outputSampleRate;
	ma_decoder_uninit(&decoder);
	free(data);
	if (result != MA_SUCCESS) {
		return 0.0f;
	}

	if (sampleRate == 0) sampleRate = 44100;
	return (float)lengthInFrames / (float)sampleRate * 1000.0f;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::closeAnySamplesUsingFile(const void *fileToClose)
{
	// GeneralsX @bugfix meerzulee 19/07/2026 FP env guard for audio entry point (see #215)
	ScopedFPUGuard fpuGuard;
	// miniaudio manages file lifecycle internally after decoding
}


//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::startNextLoop(MiniPlayingAudio *looping)
{
	if (looping->m_requestStop) return false;
	if (!looping->m_audioEventRTS->hasMoreLoops()) return false;

	looping->m_audioEventRTS->generateFilename();

	if (looping->m_audioEventRTS->getDelay() > MSEC_PER_LOGICFRAME_REAL) {
		looping->m_requestStop = true;

		AudioRequest *req = allocateAudioRequest();
		req->m_pendingEvent = looping->m_audioEventRTS;
		req->m_requiresCheckForSample = true;
		appendAudioRequest(req);
		return true;
	}

	// Re-initialize the sound from the new file
	const MiniPlayingAudioType type = looping->m_type;
	releaseMiniAudioHandles(looping);
	looping->m_type = type;

	AsciiString fileToPlay = looping->m_audioEventRTS->getFilename();
	ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
	if (type == MPAT_3DSample) flags = 0;

	if (initSoundFromFile(looping, fileToPlay, flags, (type == MPAT_3DSample) ? &m_sound3DGroup : &m_soundGroup)) {
		if (type == MPAT_3DSample) {
			const AudioEventInfo *info = looping->m_audioEventRTS->getAudioEventInfo();
			ma_sound_set_min_distance(looping->m_sound, (info->m_type & ST_GLOBAL) ? getAudioSettings()->m_globalMinRange : info->m_minDistance);
			ma_sound_set_max_distance(looping->m_sound, (info->m_type & ST_GLOBAL) ? getAudioSettings()->m_globalMaxRange : info->m_maxDistance);
			const Coord3D *pos = looping->m_audioEventRTS->getCurrentPosition();
			if (pos)
				ma_sound_set_position(looping->m_sound, pos->x, pos->y, pos->z);
		}
		// GeneralsX @bugfix Mr. Meeseeks 19/06/2026 - Initialize loop volume using adjustPlayingVolume
		adjustPlayingVolume(looping);
		ma_sound_start(looping->m_sound);
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::playStream(AudioEventRTS *event, ma_sound *sound)
{
	ma_sound_start(sound);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::setDeviceListenerPosition(void)
{
	ma_engine_listener_set_position(&m_engine, 0,
		m_listenerPosition.x, m_listenerPosition.y, m_listenerPosition.z);
	ma_engine_listener_set_direction(&m_engine, 0,
		m_listenerOrientation.x, m_listenerOrientation.y, m_listenerOrientation.z);
}

//-------------------------------------------------------------------------------------------------
const Coord3D *MiniAudioManager::getCurrentPositionFromEvent(AudioEventRTS *event)
{
	if (!event->isPositionalAudio()) return NULL;
	return event->getCurrentPosition();
}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isOnScreen(const Coord3D *pos) const
{
	static ICoord2D dummy;
	return TheTacticalView->worldToScreen(pos, &dummy);
}

//-------------------------------------------------------------------------------------------------
Real MiniAudioManager::getEffectiveVolume(AudioEventRTS *event) const
{
	if (!event) return 0.0f;
	Real volume = event->getVolume() * event->getVolumeShift();
	const AudioEventInfo *evInfo = event->getAudioEventInfo();
	if (evInfo && evInfo->m_soundType == AT_Music)           volume *= m_musicVolume;
	else if (evInfo && evInfo->m_soundType == AT_Streaming)  volume *= m_speechVolume;
	else if (event->isPositionalAudio())                     volume *= m_sound3DVolume;
	else                                                     volume *= m_soundVolume;
	return volume;
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::buildProviderList(void) {}
void MiniAudioManager::createListener(void) {}
void MiniAudioManager::initDelayFilter(void) {}

//-------------------------------------------------------------------------------------------------
Bool MiniAudioManager::isValidProvider(void)
{
	return (m_selectedPlaybackDevice < m_playbackDeviceCount);
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::initSamplePools(void) {}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::processRequest(AudioRequest *req)
{
	switch (req->m_request)
	{
	case AR_Play:   playAudioEvent(req); break;
	case AR_Pause:  pauseAudioEvent(req->m_handleToInteractOn); break;
	case AR_Stop:   stopAudioEvent(req->m_handleToInteractOn); break;
	}
}

//-------------------------------------------------------------------------------------------------
void MiniAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS *eventToPlay)
{
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("MiniAudio: No info for forced audio event '%s'\n", eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
	case AT_Music:     if (!isOn(AudioAffect_Music))  return; break;
	case AT_SoundEffect:
		if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D)) return;
		break;
	case AT_Streaming: if (!isOn(AudioAffect_Speech)) return; break;
	}

	AudioRequest *req = allocateAudioRequest();
	req->m_pendingEvent.Assign_No_Add_Ref(newInstance(DynamicAudioEventRTS)(*eventToPlay));
	req->m_pendingEvent->generateFilename();
	req->m_pendingEvent->generatePlayInfo();

	for (auto it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == req->m_pendingEvent->getEventName()) {
			req->m_pendingEvent->setVolume(it->second);
			break;
		}
	}

	playAudioEvent(req);
	releaseAudioRequest(req);
}

#if defined(RTS_DEBUG)
//-------------------------------------------------------------------------------------------------
void MiniAudioManager::dumpAllAssetsUsed()
{
	if (!TheGlobalData->m_preloadReport) return;

	FILE *logfile = fopen("PreloadedAssets.txt", "a+");
	if (!logfile) return;

	fprintf(logfile, "\nAudio Asset Report - BEGIN\n");
	{
		std::list<AsciiString> missingEvents;
		std::list<AsciiString> usedFiles;

		for (auto it = m_allEventsLoaded.begin(); it != m_allEventsLoaded.end(); ++it) {
			AsciiString astr = *it;
			AudioEventInfo *aei = findAudioEventInfo(astr);
			if (!aei) { missingEvents.push_back(astr); continue; }

			for (auto asIt = aei->m_attackSounds.begin(); asIt != aei->m_attackSounds.end(); ++asIt)
				usedFiles.push_back(*asIt);
			for (auto asIt = aei->m_sounds.begin(); asIt != aei->m_sounds.end(); ++asIt)
				usedFiles.push_back(*asIt);
			for (auto asIt = aei->m_decaySounds.begin(); asIt != aei->m_decaySounds.end(); ++asIt)
				usedFiles.push_back(*asIt);
			if (!aei->m_filename.isEmpty())
				usedFiles.push_back(aei->m_filename);
		}

		fprintf(logfile, "\nMissing events - BEGIN\n");
		for (auto &s : missingEvents) fprintf(logfile, "%s\n", s.str());
		fprintf(logfile, "\nMissing events - END\n");

		fprintf(logfile, "\nFiles Used - BEGIN\n");
		for (auto &s : usedFiles) fprintf(logfile, "%s\n", s.str());
		fprintf(logfile, "\nFiles Used - END\n");
	}
	fprintf(logfile, "\nAudio Asset Report - END\n");
	fclose(logfile);
}
#endif

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getNumAvailable2DSamples() const
{
	// GeneralsX @bugfix Mr. Meeseeks 27/06/2026 Fix available samples calculation to prevent voicelines culling
	UnsignedInt max2D = getAudioSettings()->m_sampleCount2D;
	UnsignedInt playing = 0;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if ((*it) && (*it)->m_type == MPAT_Sample) {
			playing++;
		}
	}
	return (max2D > playing) ? (max2D - playing) : 0;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MiniAudioManager::getNumAvailable3DSamples() const
{
	// GeneralsX @bugfix Mr. Meeseeks 01/07/2026 Fix available samples calculation to only count 3D samples
	UnsignedInt max3D = getAudioSettings()->m_sampleCount3D;
	UnsignedInt playing = 0;
	for (auto it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if ((*it) && (*it)->m_type == MPAT_3DSample) {
			playing++;
		}
	}
	return (max3D > playing) ? (max3D - playing) : 0;
}
