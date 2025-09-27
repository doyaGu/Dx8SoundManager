#include "Dx8SoundManager.h"

#include <windows.h>

#include "CKAll.h"

// External globals
extern long g_InitialVolume;
extern CKBOOL g_InitialVolumeChanged;

// Helper macro for min function (VC6 compatible)
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

// Factory function
CKERROR CreateDx8SoundManager(CKContext *context)
{
    if (!context) return CKERR_INVALIDPARAMETER;
    
    DX8SoundManager* manager = new DX8SoundManager(context);
    if (!manager) return CKERR_OUTOFMEMORY;
    
    return CK_OK;
}

//-----------------------------------------------------------------------------
// Constructor/Destructor
//-----------------------------------------------------------------------------

DX8SoundManager::DX8SoundManager(CKContext *Context) 
    : DXSoundManager(Context)
{
    m_Root = NULL;
    m_Listener = NULL;
    m_Primary = NULL;
    m_bInitialized = FALSE;
    m_bCriticalSectionInitialized = FALSE;
    
    InitializeCriticalSection();
    m_Context->RegisterNewManager(this);
}

DX8SoundManager::~DX8SoundManager()
{
    // Ensure cleanup
    OnCKEnd();
    DeleteCriticalSection();
}

//-----------------------------------------------------------------------------
// Thread Safety Helpers
//-----------------------------------------------------------------------------

void DX8SoundManager::InitializeCriticalSection()
{
    if (!m_bCriticalSectionInitialized) {
        ::InitializeCriticalSection(&m_CriticalSection);
        m_bCriticalSectionInitialized = TRUE;
    }
}

void DX8SoundManager::DeleteCriticalSection()
{
    if (m_bCriticalSectionInitialized) {
        ::DeleteCriticalSection(&m_CriticalSection);
        m_bCriticalSectionInitialized = FALSE;
    }
}

void DX8SoundManager::EnterCriticalSection() const
{
    if (m_bCriticalSectionInitialized) {
        ::EnterCriticalSection((CRITICAL_SECTION*)&m_CriticalSection);
    }
}

void DX8SoundManager::LeaveCriticalSection() const
{
    if (m_bCriticalSectionInitialized) {
        ::LeaveCriticalSection((CRITICAL_SECTION*)&m_CriticalSection);
    }
}

//-----------------------------------------------------------------------------
// Capability and Status Methods
//-----------------------------------------------------------------------------

CK_SOUNDMANAGER_CAPS DX8SoundManager::GetCaps()
{
    CKDWORD caps = CK_WAVESOUND_SETTINGS_ALL | 
                   CK_WAVESOUND_3DSETTINGS_ALL | 
                   CK_LISTENERSETTINGS_ALL | 
                   CK_WAVESOUND_3DSETTINGS_DISTANCEFACTOR | 
                   CK_WAVESOUND_3DSETTINGS_DOPPLERFACTOR;
    
    // Remove unsupported features
    caps &= ~(CK_WAVESOUND_SETTINGS_EQUALIZATION | 
              CK_WAVESOUND_SETTINGS_PRIORITY | 
              CK_LISTENERSETTINGS_EQ | 
              CK_LISTENERSETTINGS_PRIORITY | 
              CK_SOUNDMANAGER_ONFLYTYPE);
    
    return (CK_SOUNDMANAGER_CAPS)caps;
}

CKBOOL DX8SoundManager::IsInitialized()
{
    CKBOOL result;
    EnterCriticalSection();
    result = m_bInitialized && m_Root && m_Primary;
    LeaveCriticalSection();
    return result;
}

//-----------------------------------------------------------------------------
// Validation and Error Handling
//-----------------------------------------------------------------------------

CKBOOL DX8SoundManager::ValidateSource(void *source) const
{
    return source != NULL;
}

CKBOOL DX8SoundManager::ValidateDirectSound() const
{
    return m_Root != NULL && m_Primary != NULL;
}

CKERROR DX8SoundManager::HandleDirectSoundError(HRESULT hr, const char* operation) const
{
    if (SUCCEEDED(hr)) return CK_OK;
    
    if (m_Context && m_Context->IsInInterfaceMode()) {
        char errorMsg[256];
        sprintf(errorMsg, "DirectSound Error in %s: 0x%08X", operation, hr);
        m_Context->OutputToConsole(errorMsg);
    }
    
    switch (hr) {
        case DSERR_OUTOFMEMORY: return CKERR_OUTOFMEMORY;
        case DSERR_INVALIDPARAM: return CKERR_INVALIDPARAMETER;
        case DSERR_BADFORMAT: return CKERR_INVALIDFILE;
        default: return CKERR_INVALIDOPERATION;
    }
}

//-----------------------------------------------------------------------------
// Source Creation and Management
//-----------------------------------------------------------------------------

void *DX8SoundManager::CreateSource(CK_WAVESOUND_TYPE type, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed)
{
    DSBUFFERDESC dsbd;
    LPDIRECTSOUNDBUFFER buffer;
    HRESULT hr;
    
    if (!wf || bytes == 0) return NULL;
    
    if (m_Context->GetStartOptions() & CK_CONFIG_DISABLEDSOUND) {
        if (m_Context->IsInInterfaceMode()) {
            m_Context->OutputToConsole("Cannot create sound: Sound disabled");
        }
        return NULL;
    }

    if (!ValidateDirectSound()) {
        if (m_Context->IsInInterfaceMode()) {
            m_Context->OutputToConsole("Cannot create sound: Sound manager not initialized");
        }
        return NULL;
    }

    // Setup DirectSound buffer description
    ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = DSBCAPS_CTRLFREQUENCY | 
                   DSBCAPS_CTRLVOLUME | 
                   DSBCAPS_GETCURRENTPOSITION2 | 
                   DSBCAPS_GLOBALFOCUS;

    dsbd.dwBufferBytes = bytes;
    dsbd.lpwfxFormat = (WAVEFORMATEX*)wf;

    // Set type-specific flags
    if (type == CK_WAVESOUND_BACKGROUND) {
        dsbd.dwFlags |= DSBCAPS_CTRLPAN;
    } else {
        dsbd.dwFlags |= DSBCAPS_CTRL3D;
    }

    buffer = NULL;
    hr = m_Root->CreateSoundBuffer(&dsbd, &buffer, NULL);
    
    if (FAILED(hr)) {
        HandleDirectSoundError(hr, "CreateSoundBuffer");
        return NULL;
    }

    // Set frequency
    hr = buffer->SetFrequency(wf->nSamplesPerSec);
    if (FAILED(hr)) {
        buffer->Release();
        HandleDirectSoundError(hr, "SetFrequency");
        return NULL;
    }

    return buffer;
}

void *DX8SoundManager::DuplicateSource(void *source)
{
    LPDIRECTSOUNDBUFFER srcBuffer;
    LPDIRECTSOUNDBUFFER newBuffer;
    HRESULT hr;
    DWORD formatSize;
    BYTE formatStack[sizeof(WAVEFORMATEX)];
    WAVEFORMATEX *waveFormat;
    CKBOOL formatAllocated;
    DSBCAPS caps;
    DSBUFFERDESC dsbd;
    LONG volume, pan;
    DWORD frequency;
    LPDIRECTSOUND3DBUFFER src3D, new3D;
    DS3DBUFFER buffer3D;
    BYTE *srcData1, *srcData2;
    BYTE *newData1, *newData2;
    DWORD srcSize1, srcSize2;
    DWORD newSize1, newSize2;
    
    if (!ValidateSource(source) || !ValidateDirectSound()) {
        return NULL;
    }

    srcBuffer = (LPDIRECTSOUNDBUFFER)source;
    newBuffer = NULL;
    
    // First attempt: Use DirectSound's built-in duplicate function
    hr = m_Root->DuplicateSoundBuffer(srcBuffer, &newBuffer);
    
    if (SUCCEEDED(hr)) {
        return newBuffer;
    }

    // Fallback: Manual duplication
    waveFormat = (WAVEFORMATEX*)formatStack;
    formatAllocated = FALSE;
    formatSize = sizeof(formatStack);

    hr = srcBuffer->GetFormat(waveFormat, formatSize, &formatSize);
    if (hr == DSERR_INVALIDPARAM && formatSize > sizeof(formatStack)) {
        waveFormat = (WAVEFORMATEX*)new BYTE[formatSize];
        if (!waveFormat) {
            return NULL;
        }
        formatAllocated = TRUE;
        hr = srcBuffer->GetFormat(waveFormat, formatSize, NULL);
    }

    if (FAILED(hr)) {
        if (formatAllocated) {
            delete [] (BYTE*)waveFormat;
        }
        return NULL;
    }

    caps.dwSize = sizeof(DSBCAPS);
    hr = srcBuffer->GetCaps(&caps);
    if (FAILED(hr)) {
        if (formatAllocated) {
            delete [] (BYTE*)waveFormat;
        }
        return NULL;
    }

    // Create new buffer with same properties
    ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = caps.dwFlags & ~(DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE | DSBCAPS_LOCDEFER);
    dsbd.dwBufferBytes = caps.dwBufferBytes;
    dsbd.lpwfxFormat = waveFormat;

    hr = m_Root->CreateSoundBuffer(&dsbd, &newBuffer, NULL);
    if (FAILED(hr)) {
        if (formatAllocated) {
            delete [] (BYTE*)waveFormat;
        }
        return NULL;
    }

    if (formatAllocated) {
        delete [] (BYTE*)waveFormat;
    }

    // Copy properties
    if (SUCCEEDED(srcBuffer->GetVolume(&volume))) {
        newBuffer->SetVolume(volume);
    }
    if (SUCCEEDED(srcBuffer->GetPan(&pan))) {
        newBuffer->SetPan(pan);
    }
    if (SUCCEEDED(srcBuffer->GetFrequency(&frequency))) {
        newBuffer->SetFrequency(frequency);
    }

    // Copy 3D parameters if applicable
    src3D = NULL;
    new3D = NULL;
    
    if (SUCCEEDED(srcBuffer->QueryInterface(IID_IDirectSound3DBuffer, (LPVOID*)&src3D)) &&
        SUCCEEDED(newBuffer->QueryInterface(IID_IDirectSound3DBuffer, (LPVOID*)&new3D))) {
        
        if (SUCCEEDED(src3D->GetAllParameters(&buffer3D))) {
            new3D->SetAllParameters(&buffer3D, DS3D_IMMEDIATE);
        }
    }

    // Safe cleanup of interfaces
    if (src3D) src3D->Release();
    if (new3D) new3D->Release();

    // Copy buffer content
    srcData1 = NULL; srcData2 = NULL;
    newData1 = NULL; newData2 = NULL;
    srcSize1 = 0; srcSize2 = 0;
    newSize1 = 0; newSize2 = 0;

    if (SUCCEEDED(srcBuffer->Lock(0, 0, (LPVOID*)&srcData1, &srcSize1, 
                                 (LPVOID*)&srcData2, &srcSize2, DSBLOCK_ENTIREBUFFER))) {
        
        if (SUCCEEDED(newBuffer->Lock(0, 0, (LPVOID*)&newData1, &newSize1,
                                     (LPVOID*)&newData2, &newSize2, DSBLOCK_ENTIREBUFFER))) {
            
            // Copy primary segment
            if (srcData1 && newData1 && srcSize1 > 0) {
                memcpy(newData1, srcData1, min(srcSize1, newSize1));
            }
            
            // Copy secondary segment (if buffer wraps)
            if (srcData2 && newData2 && srcSize2 > 0) {
                memcpy(newData2, srcData2, min(srcSize2, newSize2));
            }
            
            newBuffer->Unlock(newData1, newSize1, newData2, newSize2);
        } else {
            // Failed to lock new buffer, cleanup
            newBuffer->Release();
            newBuffer = NULL;
        }
        
        srcBuffer->Unlock(srcData1, srcSize1, srcData2, srcSize2);
    }

    return newBuffer;
}

void DX8SoundManager::ReleaseSource(void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    
    if (!ValidateSource(source)) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    buffer->Stop();
    buffer->Release();
}

//-----------------------------------------------------------------------------
// Playback Control
//-----------------------------------------------------------------------------

void DX8SoundManager::InternalPause(void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    
    if (!ValidateSource(source)) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    buffer->Stop();
}

void DX8SoundManager::InternalPlay(void *source, CKBOOL loop)
{
    LPDIRECTSOUNDBUFFER buffer;
    DWORD flags;
    
    if (!ValidateSource(source)) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    flags = loop ? DSBPLAY_LOOPING : 0;
    buffer->Play(0, 0, flags);
}

void DX8SoundManager::Play(CKWaveSound *ws, void *source, CKBOOL loop)
{
    LPDIRECTSOUNDBUFFER buffer = NULL;
    SoundMinion *minion;
    
    if (!ValidateSource(source)) return;
    
    if (ws) {
        // Normal sound
        buffer = (LPDIRECTSOUNDBUFFER)source;
        m_SoundsPlaying.AddIfNotHere(ws->GetID());
    } else {
        // Minion sound
        minion = (SoundMinion*)source;
        if (minion && minion->m_Source) {
            buffer = (LPDIRECTSOUNDBUFFER)minion->m_Source;
        }
    }
    
    if (buffer) {
        InternalPlay(buffer, loop);
    }
}

void DX8SoundManager::Pause(CKWaveSound *ws, void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    
    if (!ValidateSource(source)) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    InternalPause(buffer);
}

void DX8SoundManager::SetPlayPosition(void *source, int pos)
{
    LPDIRECTSOUNDBUFFER buffer;
    
    if (!ValidateSource(source) || pos < 0) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    buffer->SetCurrentPosition((DWORD)pos);
}

int DX8SoundManager::GetPlayPosition(void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    DWORD playPos = 0, writePos = 0;
    
    if (!ValidateSource(source)) return 0;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    
    if (SUCCEEDED(buffer->GetCurrentPosition(&playPos, &writePos))) {
        return (int)playPos;
    }
    
    return 0;
}

CKBOOL DX8SoundManager::IsPlaying(void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    DWORD status = 0;
    
    if (!ValidateSource(source)) return FALSE;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    
    if (SUCCEEDED(buffer->GetStatus(&status))) {
        return (status & DSBSTATUS_PLAYING) ? TRUE : FALSE;
    }
    
    return FALSE;
}

//-----------------------------------------------------------------------------
// PCM Buffer Information
//-----------------------------------------------------------------------------

CKERROR DX8SoundManager::SetWaveFormat(void *source, CKWaveFormat &wf)
{
    LPDIRECTSOUNDBUFFER buffer;
    HRESULT hr;
    
    if (!ValidateSource(source)) return CKERR_INVALIDPARAMETER;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    hr = buffer->SetFormat((WAVEFORMATEX*)&wf);
    return HandleDirectSoundError(hr, "SetWaveFormat");
}

CKERROR DX8SoundManager::GetWaveFormat(void *source, CKWaveFormat &wf)
{
    LPDIRECTSOUNDBUFFER buffer;
    HRESULT hr;
    
    if (!ValidateSource(source)) return CKERR_INVALIDPARAMETER;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    hr = buffer->GetFormat((WAVEFORMATEX*)&wf, sizeof(CKWaveFormat), NULL);
    return HandleDirectSoundError(hr, "GetWaveFormat");
}

int DX8SoundManager::GetWaveSize(void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    DSBCAPS caps;
    
    if (!ValidateSource(source)) return 0;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    ZeroMemory(&caps, sizeof(DSBCAPS));
    caps.dwSize = sizeof(DSBCAPS);
    
    if (SUCCEEDED(buffer->GetCaps(&caps))) {
        return (int)caps.dwBufferBytes;
    }
    
    return 0;
}

//-----------------------------------------------------------------------------
// Buffer Access
//-----------------------------------------------------------------------------

CKERROR DX8SoundManager::Lock(void *source, CKDWORD dwWriteCursor, CKDWORD dwNumBytes, 
                             void **pvAudioPtr1, CKDWORD *dwAudioBytes1, 
                             void **pvAudioPtr2, CKDWORD *dwAudioBytes2, 
                             CK_WAVESOUND_LOCKMODE dwFlags)
{
    LPDIRECTSOUNDBUFFER buffer;
    HRESULT hr;
    
    if (!ValidateSource(source) || !pvAudioPtr1 || !dwAudioBytes1) {
        return CKERR_INVALIDPARAMETER;
    }
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    hr = buffer->Lock(dwWriteCursor, dwNumBytes, pvAudioPtr1, 
                     (DWORD*)dwAudioBytes1, 
                     pvAudioPtr2, (DWORD*)dwAudioBytes2, 
                     dwFlags);
    return HandleDirectSoundError(hr, "Lock");
}

CKERROR DX8SoundManager::Unlock(void *source, void *pvAudioPtr1, CKDWORD dwNumBytes1, 
                                void *pvAudioPtr2, CKDWORD dwAudioBytes2)
{
    LPDIRECTSOUNDBUFFER buffer;
    HRESULT hr;
    
    if (!ValidateSource(source)) return CKERR_INVALIDPARAMETER;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    hr = buffer->Unlock(pvAudioPtr1, dwNumBytes1, pvAudioPtr2, dwAudioBytes2);
    return HandleDirectSoundError(hr, "Unlock");
}

//-----------------------------------------------------------------------------
// Type Management
//-----------------------------------------------------------------------------

void DX8SoundManager::SetType(void *source, CK_WAVESOUND_TYPE type)
{
    if (!ValidateSource(source)) return;
    
    if (m_Context->IsInInterfaceMode()) {
        m_Context->OutputToConsole("Warning: DirectX SoundManager doesn't support on-the-fly type changes");
    }
}

CK_WAVESOUND_TYPE DX8SoundManager::GetType(void *source)
{
    LPDIRECTSOUNDBUFFER buffer;
    DSBCAPS caps;
    
    if (!ValidateSource(source)) return (CK_WAVESOUND_TYPE)0;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    ZeroMemory(&caps, sizeof(DSBCAPS));
    caps.dwSize = sizeof(DSBCAPS);
    
    if (SUCCEEDED(buffer->GetCaps(&caps))) {
        return (caps.dwFlags & DSBCAPS_CTRL3D) ? CK_WAVESOUND_POINT : CK_WAVESOUND_BACKGROUND;
    }
    
    return CK_WAVESOUND_BACKGROUND;
}

//-----------------------------------------------------------------------------
// Settings Management
//-----------------------------------------------------------------------------

void DX8SoundManager::UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, 
                                     CKWaveSoundSettings &settings, CKBOOL set)
{
    LPDIRECTSOUNDBUFFER buffer;
    WAVEFORMATEX wf;
    DWORD newFreq;
    LONG volume, pan;
    DWORD frequency;
    
    if (!ValidateSource(source)) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    
    if (set) {
        // Set settings
        if (settingsoptions & CK_WAVESOUND_SETTINGS_GAIN) {
            buffer->SetVolume(FloatToDb(settings.m_Gain));
        }
        
        if (settingsoptions & CK_WAVESOUND_SETTINGS_PITCH) {
            if (SUCCEEDED(buffer->GetFormat(&wf, sizeof(WAVEFORMATEX), NULL))) {
                newFreq = (DWORD)(wf.nSamplesPerSec * settings.m_Pitch);
                buffer->SetFrequency(newFreq);
            }
        }
        
        if ((settingsoptions & CK_WAVESOUND_SETTINGS_PAN) && 
            (GetType(source) == CK_WAVESOUND_BACKGROUND)) {
            buffer->SetPan(FloatPanningToDb(settings.m_Pan));
        }
    } else {
        // Get settings
        if (settingsoptions & CK_WAVESOUND_SETTINGS_GAIN) {
            if (SUCCEEDED(buffer->GetVolume(&volume))) {
                settings.m_Gain = DbToFloat(volume);
            }
        }
        
        if (settingsoptions & CK_WAVESOUND_SETTINGS_PITCH) {
            if (SUCCEEDED(buffer->GetFormat(&wf, sizeof(WAVEFORMATEX), NULL)) &&
                SUCCEEDED(buffer->GetFrequency(&frequency))) {
                settings.m_Pitch = (float)frequency / wf.nSamplesPerSec;
            }
        }
        
        if (settingsoptions & CK_WAVESOUND_SETTINGS_PAN) {
            if (SUCCEEDED(buffer->GetPan(&pan))) {
                settings.m_Pan = DbPanningToFloat(pan);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// 3D Settings Management
//-----------------------------------------------------------------------------

void DX8SoundManager::Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, 
                                       CKWaveSound3DSettings &settings, CKBOOL set)
{
    LPDIRECTSOUNDBUFFER buffer;
    LPDIRECTSOUND3DBUFFER buffer3D;
    HRESULT hr;
    DWORD inAngle, outAngle;
    LONG outsideVolume;
    DWORD mode;
    D3DVECTOR pos, vel, orient;
    
    if (!ValidateSource(source)) return;
    
    buffer = (LPDIRECTSOUNDBUFFER)source;
    buffer3D = NULL;
    
    hr = buffer->QueryInterface(IID_IDirectSound3DBuffer, (VOID**)&buffer3D);
    if (FAILED(hr)) return;
    
    if (set) {
        // Set 3D settings
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_CONE) {
            buffer3D->SetConeAngles((DWORD)settings.m_InAngle, 
                                   (DWORD)settings.m_OutAngle, DS3D_IMMEDIATE);
            buffer3D->SetConeOutsideVolume(FloatToDb(settings.m_OutsideGain), DS3D_IMMEDIATE);
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_MINMAXDISTANCE) {
            buffer3D->SetMinDistance(settings.m_MinDistance, DS3D_IMMEDIATE);
            buffer3D->SetMaxDistance(settings.m_MaxDistance, DS3D_IMMEDIATE);
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_POSITION) {
            buffer3D->SetPosition(settings.m_Position.x, settings.m_Position.y, 
                                 settings.m_Position.z, DS3D_IMMEDIATE);
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_VELOCITY) {
            buffer3D->SetVelocity(settings.m_Velocity.x, settings.m_Velocity.y, 
                                 settings.m_Velocity.z, DS3D_IMMEDIATE);
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_ORIENTATION) {
            buffer3D->SetConeOrientation(settings.m_OrientationDir.x, settings.m_OrientationDir.y, 
                                        settings.m_OrientationDir.z, DS3D_IMMEDIATE);
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_HEADRELATIVE) {
            mode = settings.m_HeadRelative ? DS3DMODE_HEADRELATIVE : DS3DMODE_NORMAL;
            buffer3D->SetMode(mode, DS3D_IMMEDIATE);
        }
    } else {
        // Get 3D settings
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_CONE) {
            if (SUCCEEDED(buffer3D->GetConeAngles(&inAngle, &outAngle))) {
                settings.m_InAngle = (float)inAngle;
                settings.m_OutAngle = (float)outAngle;
            }
            if (SUCCEEDED(buffer3D->GetConeOutsideVolume(&outsideVolume))) {
                settings.m_OutsideGain = DbToFloat(outsideVolume);
            }
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_MINMAXDISTANCE) {
            buffer3D->GetMinDistance(&settings.m_MinDistance);
            buffer3D->GetMaxDistance(&settings.m_MaxDistance);
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_HEADRELATIVE) {
            if (SUCCEEDED(buffer3D->GetMode(&mode))) {
                settings.m_HeadRelative = (mode == DS3DMODE_HEADRELATIVE) ? 1 : 0;
            }
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_POSITION) {
            if (SUCCEEDED(buffer3D->GetPosition(&pos))) {
                settings.m_Position.Set(pos.x, pos.y, pos.z);
            }
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_VELOCITY) {
            if (SUCCEEDED(buffer3D->GetVelocity(&vel))) {
                settings.m_Velocity.Set(vel.x, vel.y, vel.z);
            }
        }
        
        if (settingsoptions & CK_WAVESOUND_3DSETTINGS_ORIENTATION) {
            if (SUCCEEDED(buffer3D->GetConeOrientation(&orient))) {
                settings.m_OrientationDir.Set(orient.x, orient.y, orient.z);
            }
        }
    }

    buffer3D->Release();
}

//-----------------------------------------------------------------------------
// Listener Settings
//-----------------------------------------------------------------------------

void DX8SoundManager::UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, 
                                             CKListenerSettings &settings, CKBOOL set)
{
    LONG volume;
    
    if (!m_Listener) return;
    
    if (set) {
        if (settingsoptions & CK_LISTENERSETTINGS_DISTANCE) {
            m_Listener->SetDistanceFactor(settings.m_DistanceFactor, DS3D_IMMEDIATE);
        }
        if (settingsoptions & CK_LISTENERSETTINGS_DOPPLER) {
            m_Listener->SetDopplerFactor(settings.m_DopplerFactor, DS3D_IMMEDIATE);
        }
        if (settingsoptions & CK_LISTENERSETTINGS_ROLLOFF) {
            m_Listener->SetRolloffFactor(settings.m_RollOff, DS3D_IMMEDIATE);
        }
        if (settingsoptions & CK_LISTENERSETTINGS_GAIN) {
            if (m_Primary) {
                m_Primary->SetVolume(FloatToDb(settings.m_GlobalGain));
                g_InitialVolumeChanged = TRUE;
            }
        }
    } else {
        if (settingsoptions & CK_LISTENERSETTINGS_DISTANCE) {
            m_Listener->GetDistanceFactor(&settings.m_DistanceFactor);
        }
        if (settingsoptions & CK_LISTENERSETTINGS_DOPPLER) {
            m_Listener->GetDopplerFactor(&settings.m_DopplerFactor);
        }
        if (settingsoptions & CK_LISTENERSETTINGS_ROLLOFF) {
            m_Listener->GetRolloffFactor(&settings.m_RollOff);
        }
        if (settingsoptions & CK_LISTENERSETTINGS_GAIN) {
            if (m_Primary) {
                if (SUCCEEDED(m_Primary->GetVolume(&volume))) {
                    settings.m_GlobalGain = DbToFloat(volume);
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// 3D Positioning
//-----------------------------------------------------------------------------

void DX8SoundManager::PositionSource(LPDIRECTSOUNDBUFFER psource, CK3dEntity *ent, 
                                     const VxVector &position, const VxVector &direction, 
                                     VxVector &oldpos)
{
    LPDIRECTSOUND3DBUFFER source3D;
    HRESULT hr;
    VxVector pos, vel, dir;
    
    if (!psource) return;
    
    source3D = NULL;
    hr = psource->QueryInterface(IID_IDirectSound3DBuffer, (VOID**)&source3D);
    if (FAILED(hr)) return;
    
    // Calculate position
    pos = position;
    if (ent) {
        ent->Transform(&pos, &position);
    }
    
    // Calculate velocity
    vel = pos - oldpos;
    
    // Calculate orientation
    dir = direction;
    if (ent) {
        ent->TransformVector(&dir, &direction);
    }
    
    // Update 3D properties
    source3D->SetPosition(pos.x, pos.y, pos.z, DS3D_IMMEDIATE);
    source3D->SetVelocity(vel.x, vel.y, vel.z, DS3D_IMMEDIATE);
    source3D->SetConeOrientation(dir.x, dir.y, dir.z, DS3D_IMMEDIATE);
    
    oldpos = pos;
    source3D->Release();
}

//-----------------------------------------------------------------------------
// Lifecycle Management
//-----------------------------------------------------------------------------

CKERROR DX8SoundManager::PostClearAll()
{
    CKERROR result;
    
    EnterCriticalSection();
    
    result = CKSoundManager::PostClearAll();
    m_SoundsPlaying.Clear();
    ReleaseMinions();
    RegisterAttribute();
    
    LeaveCriticalSection();
    return result;
}

CKERROR DX8SoundManager::OnCKInit()
{
    HRESULT hr;
    HWND mainWindow;
    DSBUFFERDESC dsbdesc;
    WAVEFORMATEX wfx;
    int soundsCount, i;
    CK_ID *soundIds;
    CKWaveSound *ws;
    
    if (m_Context->GetStartOptions() & CK_CONFIG_DISABLEDSOUND) {
        return CK_OK;
    }

    EnterCriticalSection();
    
    hr = S_OK;

#ifdef CK_LIB
    CKBOOL comInitialized = FALSE;
    HRESULT coInitResult = CoInitialize(NULL);
    if (FAILED(coInitResult)) {
        LeaveCriticalSection();
        return CKERR_GENERIC;
    }
    comInitialized = TRUE;

    hr = CoCreateInstance(CLSID_DirectSound, NULL, CLSCTX_ALL, 
                         IID_IDirectSound, (void**)&m_Root);
    if (FAILED(hr)) {
        if (m_Context->GetStartOptions() & CK_CONFIG_DOWARN) {
            MessageBox(NULL, "DirectX Sound Engine Initialization Failed", "Warning", MB_OK);
        }
        CoUninitialize();
        comInitialized = FALSE;
        LeaveCriticalSection();
        return HandleDirectSoundError(hr, "CoCreateInstance");
    }

    hr = m_Root->Initialize(NULL);
#else
    hr = DirectSoundCreate(NULL, &m_Root, NULL);
#endif

    const char *initOperation;
#ifdef CK_LIB
    initOperation = "IDirectSound::Initialize";
#else
    initOperation = "DirectSoundCreate";
#endif

    if (FAILED(hr)) {
        CleanupDirectSoundResources();
        if (m_Context->GetStartOptions() & CK_CONFIG_DOWARN) {
            MessageBox(NULL, "DirectX Sound Engine Initialization Failed", "Warning", MB_OK);
        }
#ifdef CK_LIB
        if (comInitialized) {
            CoUninitialize();
            comInitialized = FALSE;
        }
#endif
        LeaveCriticalSection();
        return HandleDirectSoundError(hr, initOperation);
    }

    // Set cooperative level
    mainWindow = (HWND)m_Context->GetMainWindow();
    hr = m_Root->SetCooperativeLevel(mainWindow, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        CleanupDirectSoundResources();
        if (m_Context->GetStartOptions() & CK_CONFIG_DOWARN) {
            MessageBox(NULL, "DirectX Cooperative Level Failed", "Warning", MB_OK);
        }
#ifdef CK_LIB
        if (comInitialized) {
            CoUninitialize();
            comInitialized = FALSE;
        }
#endif
        LeaveCriticalSection();
        return HandleDirectSoundError(hr, "SetCooperativeLevel");
    }

    // Create primary buffer
    ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
    dsbdesc.dwSize = sizeof(DSBUFFERDESC);
    dsbdesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRL3D;

    hr = m_Root->CreateSoundBuffer(&dsbdesc, &m_Primary, NULL);
    if (FAILED(hr)) {
        CleanupDirectSoundResources();
        if (m_Context->GetStartOptions() & CK_CONFIG_DOWARN) {
            MessageBox(NULL, "DirectX Primary Buffer Failed", "Warning", MB_OK);
        }
#ifdef CK_LIB
        if (comInitialized) {
            CoUninitialize();
            comInitialized = FALSE;
        }
#endif
        LeaveCriticalSection();
        return HandleDirectSoundError(hr, "CreateSoundBuffer(Primary)");
    }

    // Store initial volume
    m_Primary->GetVolume(&g_InitialVolume);

    // Get listener interface
    hr = m_Primary->QueryInterface(IID_IDirectSound3DListener, (VOID**)&m_Listener);
    if (FAILED(hr)) {
        CleanupDirectSoundResources();
        if (m_Context->GetStartOptions() & CK_CONFIG_DOWARN) {
            MessageBox(NULL, "DirectX Listener Failed", "Warning", MB_OK);
        }
#ifdef CK_LIB
        if (comInitialized) {
            CoUninitialize();
            comInitialized = FALSE;
        }
#endif
        LeaveCriticalSection();
        return HandleDirectSoundError(hr, "QueryInterface(Listener)");
    }

    // Set primary buffer format
    ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = DEFAULT_CHANNELS;
    wfx.nSamplesPerSec = DEFAULT_SAMPLE_RATE;
    wfx.wBitsPerSample = DEFAULT_BITS_PER_SAMPLE;
    wfx.nBlockAlign = wfx.wBitsPerSample / 8 * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    hr = m_Primary->SetFormat(&wfx);
    if (FAILED(hr)) {
        // Non-fatal error, continue with default format
        if (m_Context->IsInInterfaceMode()) {
            m_Context->OutputToConsole("Warning: Could not set preferred audio format");
        }
    }

    RegisterAttribute();

    // Recreate existing sounds
    soundsCount = m_Context->GetObjectsCountByClassID(CKCID_WAVESOUND);
    if (soundsCount > 0) {
        soundIds = m_Context->GetObjectsListByClassID(CKCID_WAVESOUND);
        for (i = 0; i < soundsCount; ++i) {
            ws = (CKWaveSound*)m_Context->GetObject(soundIds[i]);
            if (ws) {
                ws->Recreate();
            }
        }
    }

    // Start primary buffer playback
    m_Primary->Play(0, 0, DSBPLAY_LOOPING);
    
    m_bInitialized = TRUE;
    LeaveCriticalSection();
    return CK_OK;
}

CKERROR DX8SoundManager::OnCKEnd()
{
    if (m_Context->GetStartOptions() & CK_CONFIG_DISABLEDSOUND) {
        return CK_OK;
    }

    EnterCriticalSection();
    
    // Stop all sounds and clean up
    StopAllPlayingSounds();
    CleanupDirectSoundResources();
    
    m_bInitialized = FALSE;
    
#ifdef CK_LIB
    CoUninitialize();
#endif

    LeaveCriticalSection();
    return CK_OK;
}

void DX8SoundManager::CleanupDirectSoundResources()
{
    // Release listener
    if (m_Listener) {
        m_Listener->Release();
        m_Listener = NULL;
    }

    // Stop and release primary buffer
    if (m_Primary) {
        m_Primary->Stop();
        m_Primary->Release();
        m_Primary = NULL;
    }

    // Release root interface
    if (m_Root) {
        m_Root->Release();
        m_Root = NULL;
    }
}

void DX8SoundManager::StopAllPlayingSounds()
{
    int soundsCount, i;
    CK_ID *soundIds;
    CKWaveSound *ws;
    
    // Release existing sounds
    soundsCount = m_Context->GetObjectsCountByClassID(CKCID_WAVESOUND);
    if (soundsCount > 0) {
        soundIds = m_Context->GetObjectsListByClassID(CKCID_WAVESOUND);
        for (i = 0; i < soundsCount; ++i) {
            ws = (CKWaveSound*)m_Context->GetObject(soundIds[i]);
            if (ws) {
                ws->Release();
            }
        }
    }
}

CKERROR DX8SoundManager::PostProcess()
{
    float deltaTime;
    CKBOOL somethingIsPlayingIn3D;
    CK_ID *it;
    CKWaveSound *ws;
    SoundMinion **itm;
    CK3dEntity *ent;
    CK3dEntity *listener;
    const VxMatrix *mat;
    const VxVector4 *pos, *dir, *up;
    VxVector velocity;
    
    if (!ValidateDirectSound()) return CK_OK;
    
    EnterCriticalSection();
    
    deltaTime = m_Context->GetTimeManager()->GetLastDeltaTime();
    somethingIsPlayingIn3D = FALSE;

    // Update playing sounds
    for (it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End();) {
        ws = (CKWaveSound*)m_Context->GetObject(*it);
        
        if (ws && ws->IsPlaying()) {
            // Handle file streaming
            if (ws->GetFileStreaming() && !(ws->GetState() & CK_WAVESOUND_STREAMFULLYLOADED)) {
                ws->WriteDataFromReader();
            }

            // Update fade
            ws->UpdateFade();

            // Update 3D position
            if (!(ws->GetType() & CK_WAVESOUND_BACKGROUND)) {
                somethingIsPlayingIn3D = TRUE;
                ws->UpdatePosition(deltaTime);
            }

            ++it;
        } else {
            it = m_SoundsPlaying.Remove(it);
        }
    }

    // Update minions
    for (itm = m_Minions.Begin(); itm != m_Minions.End(); ++itm) {
        if (IsPlaying((*itm)->m_Source)) {
            somethingIsPlayingIn3D = TRUE;
            
            if ((*itm)->m_Entity) {
                ent = (CK3dEntity*)m_Context->GetObject((*itm)->m_Entity);
                if (ent) {
                    PositionSource((LPDIRECTSOUNDBUFFER)(*itm)->m_Source, 
                                  ent, (*itm)->m_Position, (*itm)->m_Direction, (*itm)->m_OldPosition);
                }
            }
        }
    }

    // Update listener if something is playing in 3D
    if (somethingIsPlayingIn3D) {
        listener = GetListener();
        if (listener) {
            mat = &listener->GetWorldMatrix();
            pos = &(*mat)[3];
            dir = &(*mat)[2];
            up = &(*mat)[1];
            
            velocity = VxVector(pos->x, pos->y, pos->z) - m_LastListenerPosition;
            m_LastListenerPosition.Set(pos->x, pos->y, pos->z);
            
            m_Listener->SetPosition(pos->x, pos->y, pos->z, DS3D_DEFERRED);
            m_Listener->SetVelocity(velocity.x, velocity.y, velocity.z, DS3D_DEFERRED);
            m_Listener->SetOrientation(dir->x, dir->y, dir->z, up->x, up->y, up->z, DS3D_DEFERRED);
        }
    }

    // Commit deferred settings
    if (m_Listener) {
        m_Listener->CommitDeferredSettings();
    }

    // Process minions (cleanup finished ones)
    ProcessMinions();
    
    LeaveCriticalSection();
    return CK_OK;
}

CKERROR DX8SoundManager::OnCKReset()
{
    CK_ID *it;
    CKWaveSound *ws;
    
    if (!ValidateDirectSound()) return CK_OK;
    
    EnterCriticalSection();

    // Stop all playing sounds
    for (it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it) {
        ws = (CKWaveSound*)m_Context->GetObject(*it);
        if (ws && ws->m_Source) {
            ws->InternalStop();
        }
    }

    m_SoundsPlaying.Clear();
    ReleaseMinions();

    // Restore initial volume if changed
    if (g_InitialVolumeChanged && m_Primary) {
        m_Primary->SetVolume(g_InitialVolume);
    }

    // Commit listener changes
    if (m_Listener) {
        m_Listener->CommitDeferredSettings();
    }

    LeaveCriticalSection();
    return CK_OK;
}

//-----------------------------------------------------------------------------
// Utility Functions
//-----------------------------------------------------------------------------

void Dx8PositionSource(LPDIRECTSOUNDBUFFER psource, CK3dEntity *ent, 
                      const VxVector &position, const VxVector &direction, 
                      VxVector &oldpos)
{
    LPDIRECTSOUND3DBUFFER source3D;
    HRESULT hr;
    VxVector pos, vel, dir;
    
    if (!psource) return;
    
    source3D = NULL;
    hr = psource->QueryInterface(IID_IDirectSound3DBuffer, (VOID**)&source3D);
    if (FAILED(hr)) return;
    
    // Calculate position
    pos = position;
    if (ent) {
        ent->Transform(&pos, &position);
    }
    
    // Calculate velocity
    vel = pos - oldpos;
    
    // Calculate orientation
    dir = direction;
    if (ent) {
        ent->TransformVector(&dir, &direction);
    }
    
    // Update 3D properties
    source3D->SetPosition(pos.x, pos.y, pos.z, DS3D_IMMEDIATE);
    source3D->SetVelocity(vel.x, vel.y, vel.z, DS3D_IMMEDIATE);
    source3D->SetConeOrientation(dir.x, dir.y, dir.z, DS3D_IMMEDIATE);
    
    oldpos = pos;
    source3D->Release();
}

CKBOOL IsSourcePlaying(LPDIRECTSOUNDBUFFER source)
{
    DWORD status = 0;
    
    if (!source) return FALSE;
    
    if (SUCCEEDED(source->GetStatus(&status))) {
        return (status & DSBSTATUS_PLAYING) ? TRUE : FALSE;
    }
    
    return FALSE;
}