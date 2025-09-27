#ifndef DX8SOUNDMANAGER_H
#define DX8SOUNDMANAGER_H

#include "DxSoundManager.h"

#define DIRECTSOUND_VERSION 0x0800
#include <dsound.h>

// Constants for better maintainability
#define DEFAULT_SAMPLE_RATE     22050
#define DEFAULT_CHANNELS        2
#define DEFAULT_BITS_PER_SAMPLE 16
#define MINIMUM_VOLUME_DB       -10000
#define MAXIMUM_VOLUME_DB       0

class DX8SoundManager : public DXSoundManager
{
    friend class CKWaveSound;

public:
    // Constructor/Destructor
    DX8SoundManager(CKContext *Context);
    virtual ~DX8SoundManager();

    // Get the caps of the sound manager
    virtual CK_SOUNDMANAGER_CAPS GetCaps();

    // Creation and management
    virtual void *CreateSource(CK_WAVESOUND_TYPE flags, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed);
    virtual void *DuplicateSource(void *source);
    virtual void ReleaseSource(void *source);

    // Playback control
    virtual void Play(CKWaveSound *ws, void *source, CKBOOL loop);
    virtual void Pause(CKWaveSound *ws, void *source);
    virtual void SetPlayPosition(void *source, int pos);
    virtual int GetPlayPosition(void *source);
    virtual CKBOOL IsPlaying(void *source);

    // PCM Buffer Information
    virtual CKERROR SetWaveFormat(void *source, CKWaveFormat &wf);
    virtual CKERROR GetWaveFormat(void *source, CKWaveFormat &wf);
    virtual int GetWaveSize(void *source);

    // Buffer access
    virtual CKERROR Lock(void *source, CKDWORD dwWriteCursor, CKDWORD dwNumBytes, 
                        void **pvAudioPtr1, CKDWORD *dwAudioBytes1, 
                        void **pvAudioPtr2, CKDWORD *dwAudioBytes2, 
                        CK_WAVESOUND_LOCKMODE dwFlags);
    virtual CKERROR Unlock(void *source, void *pvAudioPtr1, CKDWORD dwNumBytes1, 
                          void *pvAudioPtr2, CKDWORD dwAudioBytes2);

    // 2D/3D Members Functions
    virtual void SetType(void *source, CK_WAVESOUND_TYPE type);
    virtual CK_WAVESOUND_TYPE GetType(void *source);

    // 2D/3D Settings
    virtual void UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, 
                               CKWaveSoundSettings &settings, CKBOOL set /* = TRUE */);

    // 3D Settings
    virtual void Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, 
                                 CKWaveSound3DSettings &settings, CKBOOL set /* = TRUE */);

    // Listener settings
    virtual void UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, 
                                       CKListenerSettings &settings, CKBOOL set /* = TRUE */);

    // Lifecycle management
    virtual CKERROR OnCKInit();
    virtual CKERROR OnCKEnd();
    virtual CKERROR OnCKReset();
    virtual CKERROR PostClearAll();
    virtual CKERROR PostProcess();

    // Status
    virtual CKBOOL IsInitialized();

protected:
    // Internal helper methods
    void InternalPause(void *source);
    void InternalPlay(void *source, CKBOOL loop /* = FALSE */);
    
    // Source positioning for 3D audio
    void PositionSource(LPDIRECTSOUNDBUFFER psource, CK3dEntity *ent, 
                       const VxVector &position, const VxVector &direction, 
                       VxVector &oldpos);
    
    // Resource validation
    CKBOOL ValidateSource(void *source) const;
    CKBOOL ValidateDirectSound() const;
    
    // Error handling helpers
    CKERROR HandleDirectSoundError(HRESULT hr, const char* operation) const;
    
    // Cleanup helpers
    void CleanupDirectSoundResources();
    void StopAllPlayingSounds();

private:
    // DirectSound interfaces
    LPDIRECTSOUND m_Root;
    LPDIRECTSOUNDBUFFER m_Primary;
    LPDIRECTSOUND3DLISTENER m_Listener;
    
    // Internal state
    CKBOOL m_bInitialized;
    VxVector m_LastListenerPosition;
    
    // Thread safety (if needed in multi-threaded scenarios)
    CRITICAL_SECTION m_CriticalSection;
    CKBOOL m_bCriticalSectionInitialized;
    
    // Helper methods for thread safety
    void InitializeCriticalSection();
    void DeleteCriticalSection();
    void EnterCriticalSection() const;
    void LeaveCriticalSection() const;

    // Prevent copy construction and assignment (VC6 style)
    DX8SoundManager(const DX8SoundManager&);
    DX8SoundManager& operator=(const DX8SoundManager&);
};

// Utility functions for 3D positioning
void Dx8PositionSource(LPDIRECTSOUNDBUFFER psource, CK3dEntity *ent, 
                      const VxVector &position, const VxVector &direction, 
                      VxVector &oldpos);

// Helper function to check if source is playing
CKBOOL IsSourcePlaying(LPDIRECTSOUNDBUFFER source);

#endif // DX8SOUNDMANAGER_H