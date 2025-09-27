#include "DxSoundManager.h"
#include <math.h>

// Global variables for initial volume tracking
long g_InitialVolume = -600;
CKBOOL g_InitialVolumeChanged = FALSE;

// Plugin export info
CKPluginInfo g_PluginInfo;
char *DXSoundManagerName = "DirectX Sound Manager";

// GUID for DX8 Sound Manager
#define DX8_SOUNDMANAGER_GUID CKGUID(0x77135393, 0x225c679a)

// Forward declaration
CKERROR CreateDx8SoundManager(CKContext *context);

//-----------------------------------------------------------------------------
// Plugin Entry Points
//-----------------------------------------------------------------------------

#ifdef CK_LIB
    #define CreateNewManager               CreateNewSoundManager
    #define RemoveManager                  RemoveSoundManager
    #define CKGetPluginInfoCount           CKGet_SoundManager_PluginInfoCount
    #define CKGetPluginInfo                CKGet_SoundManager_PluginInfo
    #define g_PluginInfo                   g_SoundManager_PluginInfo
#else
    #define CreateNewManager               CreateNewManager
    #define RemoveManager                  RemoveManager
    #define CKGetPluginInfoCount           CKGetPluginInfoCount
    #define CKGetPluginInfo                CKGetPluginInfo
    #define g_PluginInfo                   g_PluginInfo
#endif

CKERROR CreateNewManager(CKContext *context)
{
    if (!context) return CKERR_INVALIDPARAMETER;
    return CreateDx8SoundManager(context);
}

CKERROR RemoveManager(CKContext *context)
{
    DXSoundManager *manager;
    
    if (!context) return CKERR_INVALIDPARAMETER;
    
    manager = (DXSoundManager*)context->GetManagerByName(DXSoundManagerName);
    if (manager) {
        delete manager;
    }
    return CK_OK;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int Index)
{
    g_PluginInfo.m_Author = "Virtools";
    g_PluginInfo.m_Description = "DirectX Sound Manager";
    g_PluginInfo.m_Extension = "";
    g_PluginInfo.m_Type = CKPLUGIN_MANAGER_DLL;
    g_PluginInfo.m_Version = 0x000001;
    g_PluginInfo.m_InitInstanceFct = CreateNewManager;
    g_PluginInfo.m_ExitInstanceFct = RemoveManager;
    g_PluginInfo.m_GUID = DX8_SOUNDMANAGER_GUID;
    g_PluginInfo.m_Summary = DXSoundManagerName;
    return &g_PluginInfo;
}

//-----------------------------------------------------------------------------
// Audio Conversion Utility Functions
//-----------------------------------------------------------------------------

long FloatToDb(float f)
{
    if (f <= 0.0f) {
        return -10000;  /* Minimum volume in DirectSound */
    }
    if (f >= 1.0f) {
        return 0;       /* Maximum volume in DirectSound */
    }
    return (long)(2000.0 * log10(f));
}

float DbToFloat(long d)
{
    if (d <= -10000) {
        return 0.0f;
    }
    if (d >= 0) {
        return 1.0f;
    }
    return (float)pow(10.0, d / 2000.0);
}

long FloatPanningToDb(float panning)
{
    if (panning == 0.0f) {
        return 0;  /* Center */
    }
    
    /* Clamp panning to valid range */
    if (panning < -1.0f) panning = -1.0f;
    if (panning > 1.0f) panning = 1.0f;
    
    if (panning > 0.0f) {
        /* Right channel attenuation */
        return -FloatToDb(1.0f - panning);
    } else {
        /* Left channel attenuation */
        return FloatToDb(1.0f + panning);
    }
}

float DbPanningToFloat(long d)
{
    if (d > 0) {
        return 1.0f - DbToFloat(-d);
    } else if (d < 0) {
        return -1.0f + DbToFloat(d);
    }
    return 0.0f;  /* Center */
}

//-----------------------------------------------------------------------------
// DXSoundManager Implementation
//-----------------------------------------------------------------------------

DXSoundManager::DXSoundManager(CKContext *Context) 
    : CKSoundManager(Context, DXSoundManagerName)
{
    /* Base class initialization is sufficient */
}

DXSoundManager::~DXSoundManager()
{
    /* Cleanup is handled by derived classes */
}

CKERROR DXSoundManager::PostClearAll()
{
    CKERROR result = CKSoundManager::PostClearAll();
    
    m_SoundsPlaying.Clear();
    ReleaseMinions();
    RegisterAttribute();
    
    return result;
}

CKERROR DXSoundManager::OnCKPause()
{
    CK_ID *it;
    CKWaveSound *ws;
    
    /* Pause all currently playing sounds */
    for (it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it) {
        ws = (CKWaveSound*)m_Context->GetObject(*it);
        if (ws) {
            ws->Pause();
        }
    }

    /* Pause all minions */
    PauseMinions();

    return CK_OK;
}

CKERROR DXSoundManager::OnCKPlay()
{
    CK_ID *it;
    CKWaveSound *ws;
    
    /* Resume all paused sounds */
    for (it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it) {
        ws = (CKWaveSound*)m_Context->GetObject(*it);
        if (ws) {
            ws->Resume();
        }
    }

    /* Resume all minions */
    ResumeMinions();

    return CK_OK;
}

CKERROR DXSoundManager::PreLaunchScene(CKScene *OldScene, CKScene *NewScene)
{
    CK_ID *it;
    CKWaveSound *ws;
    SoundMinion **itm;
    CKSceneObject *sceneObj;
    
    if (!NewScene) return CKERR_INVALIDPARAMETER;

    /* Pause sounds that are not in the new scene */
    for (it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it) {
        ws = (CKWaveSound*)m_Context->GetObject(*it);
        if (ws && !ws->IsInScene(NewScene)) {
            ws->Pause();
        }
    }

    /* Stop minions whose models are not in the new scene */
    for (itm = m_Minions.Begin(); itm != m_Minions.End();) {
        if (!*itm) {
            itm = m_Minions.Remove(itm);
            continue;
        }

        sceneObj = (CKSceneObject*)m_Context->GetObject((*itm)->m_OriginalSound);
        if (!sceneObj || !sceneObj->IsInScene(NewScene)) {
            /* Stop and release the minion */
            Stop(NULL, (*itm)->m_Source);
            ReleaseSource((*itm)->m_Source);
            delete *itm;
            itm = m_Minions.Remove(itm);
        } else {
            ++itm;
        }
    }

    return CK_OK;
}

CKERROR DXSoundManager::SequenceToBeDeleted(CK_ID *objids, int count)
{
    CKERROR result;
    CK_ID *it;
    CKWaveSound *ws;
    SoundMinion **itm;
    CKObject *entity;
    
    if (!objids || count <= 0) return CKERR_INVALIDPARAMETER;

    /* Call base class implementation */
    result = CKSoundManager::SequenceToBeDeleted(objids, count);

    /* Stop and remove sounds that are being deleted */
    for (it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End();) {
        ws = (CKWaveSound*)m_Context->GetObject(*it);
        if (!ws || ws->IsToBeDeleted()) {
            if (ws) {
                ws->Stop();
            }
            it = m_SoundsPlaying.Remove(it);
        } else {
            ++it;
        }
    }

    /* Clean up minions with deleted entities */
    for (itm = m_Minions.Begin(); itm != m_Minions.End(); ++itm) {
        if (!*itm) continue;

        /* Check if original sound is being deleted */
        ws = (CKWaveSound*)m_Context->GetObject((*itm)->m_OriginalSound);
        if (ws && ws->IsToBeDeleted()) {
            (*itm)->m_OriginalSound = 0;
        }

        /* Check if attached entity is being deleted */
        entity = m_Context->GetObject((*itm)->m_Entity);
        if (!entity || entity->IsToBeDeleted()) {
            (*itm)->m_Entity = 0;
        }
    }

    return result;
}