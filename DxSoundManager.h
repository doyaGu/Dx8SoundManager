#ifndef DXSOUNDMANAGER_H
#define DXSOUNDMANAGER_H

#include "CKAll.h"

/**
 * @brief Abstract base class for DirectX Sound Manager implementations
 * 
 * This class provides the common interface for DirectX-based sound managers.
 * Concrete implementations (like DX8SoundManager) should inherit from this class.
 */
class DXSoundManager : public CKSoundManager
{
    friend class CKWaveSound;

public:
    // Constructor
    DXSoundManager(CKContext *Context);
    virtual ~DXSoundManager();

    // Core Sound Manager Interface (Pure Virtual)
    virtual CK_SOUNDMANAGER_CAPS GetCaps() = 0;

    // Source Creation and Management
    virtual void *CreateSource(CK_WAVESOUND_TYPE flags, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed) = 0;
    virtual void *DuplicateSource(void *source) = 0;
    virtual void ReleaseSource(void *source) = 0;

    // Playback Control
    virtual void Play(CKWaveSound *ws, void *source, CKBOOL loop) = 0;
    virtual void Pause(CKWaveSound *ws, void *source) = 0;
    virtual void Stop(CKWaveSound *w, void *source)
    {
        Pause(w, source);
        SetPlayPosition(source, 0);
    }
    virtual void SetPlayPosition(void *source, int pos) = 0;
    virtual int GetPlayPosition(void *source) = 0;
    virtual CKBOOL IsPlaying(void *source) = 0;

    // PCM Buffer Information
    virtual CKERROR SetWaveFormat(void *source, CKWaveFormat &wf) = 0;
    virtual CKERROR GetWaveFormat(void *source, CKWaveFormat &wf) = 0;
    virtual int GetWaveSize(void *source) = 0;

    // Buffer Access
    virtual CKERROR Lock(void *source, CKDWORD dwWriteCursor, CKDWORD dwNumBytes, 
                        void **pvAudioPtr1, CKDWORD *dwAudioBytes1, 
                        void **pvAudioPtr2, CKDWORD *dwAudioBytes2, 
                        CK_WAVESOUND_LOCKMODE dwFlags) = 0;
    virtual CKERROR Unlock(void *source, void *pvAudioPtr1, CKDWORD dwNumBytes1, 
                          void *pvAudioPtr2, CKDWORD dwAudioBytes2) = 0;

    // 2D/3D Audio Type Management
    virtual void SetType(void *source, CK_WAVESOUND_TYPE type) = 0;
    virtual CK_WAVESOUND_TYPE GetType(void *source) = 0;

    // Audio Settings Management
    virtual void UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, 
                               CKWaveSoundSettings &settings, CKBOOL set /* = TRUE */) = 0;

    // 3D Audio Settings
    virtual void Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, 
                                 CKWaveSound3DSettings &settings, CKBOOL set /* = TRUE */) = 0;

    // Listener Management
    virtual void UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, 
                                       CKListenerSettings &settings, CKBOOL set /* = TRUE */) = 0;

    // Lifecycle Management
    virtual CKERROR OnCKInit() = 0;
    virtual CKERROR OnCKEnd() = 0;
    virtual CKERROR OnCKReset() = 0;
    
    // Base Implementation for Common Operations
    virtual CKERROR OnCKPause();
    virtual CKERROR OnCKPlay();
    virtual CKERROR PostClearAll();
    virtual CKERROR PostProcess() = 0;

    // Status
    virtual CKBOOL IsInitialized() = 0;

    // Manager Function Mask
    virtual CKDWORD GetValidFunctionsMask()
    {
        return CKSoundManager::GetValidFunctionsMask() |
               CKMANAGER_FUNC_OnCKPlay |
               CKMANAGER_FUNC_PostClearAll |
               CKMANAGER_FUNC_OnCKInit |
               CKMANAGER_FUNC_OnCKEnd |
               CKMANAGER_FUNC_OnCKReset |
               CKMANAGER_FUNC_OnCKPause |
               CKMANAGER_FUNC_PostProcess |
               CKMANAGER_FUNC_OnSequenceToBeDeleted |
               CKMANAGER_FUNC_PreLaunchScene;
    }

    // Scene Management
    virtual CKERROR PreLaunchScene(CKScene *OldScene, CKScene *NewScene);
    virtual CKERROR SequenceToBeDeleted(CK_ID *objids, int count);

protected:
    // Common data members for all DirectX sound managers
    XObjectArray m_SoundsPlaying;  /* List of currently playing sounds */

    // Pure virtual internal methods that must be implemented
    virtual void InternalPause(void *source) = 0;
    virtual void InternalPlay(void *source, CKBOOL loop /* = FALSE */) = 0;

private:
    // Prevent copying (VC6 style - declare but don't implement)
    DXSoundManager(const DXSoundManager&);
    DXSoundManager& operator=(const DXSoundManager&);
};

// Utility functions for audio conversion
// These functions handle conversion between linear float values and decibel values
// as required by DirectSound API

/**
 * @brief Converts linear gain (0.0-1.0) to decibels
 * @param f Linear gain value (0.0 = silence, 1.0 = full volume)
 * @return Decibel value suitable for DirectSound (-10000 to 0)
 */
long FloatToDb(float f);

/**
 * @brief Converts decibels to linear gain
 * @param d Decibel value (-10000 to 0)
 * @return Linear gain value (0.0-1.0)
 */
float DbToFloat(long d);

/**
 * @brief Converts linear panning (-1.0 to 1.0) to decibels
 * @param Panning Linear panning value (-1.0 = full left, 0.0 = center, 1.0 = full right)
 * @return Decibel panning value for DirectSound
 */
long FloatPanningToDb(float Panning);

/**
 * @brief Converts decibel panning to linear panning
 * @param d Decibel panning value
 * @return Linear panning value (-1.0 to 1.0)
 */
float DbPanningToFloat(long d);

#endif /* DXSOUNDMANAGER_H */