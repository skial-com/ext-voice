#include "extension.h"
#include <iclient.h>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Extension g_extension;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_extension);

IGameConfig *g_pGameConf;

IForward *g_pBroadcastVoice;

CDetour *BroadcastVoiceDataDetour;
void (*BroadcastVoiceData_Actual)(IClient* iclient, int unknown, char *pVoiceData, long *nSamples);
void BroadcastVoiceData(IClient* iclient, int unknown, char *pVoiceData, long *nSamples) {
	int client = iclient->GetPlayerSlot() + 1;
	if(g_pBroadcastVoice) {
		cell_t result = 0;
		g_pBroadcastVoice->PushCell(client);
		g_pBroadcastVoice->Execute(&result);
		if(result == Pl_Handled || result == Pl_Stop)
			return;
	}
	BroadcastVoiceData_Actual(iclient, unknown, pVoiceData, nSamples);
}

void Extension::SDK_OnAllLoaded()
{
	g_pBroadcastVoice = forwards->CreateForward("Skial_OnBroadcastVoice", ET_Hook, 1, NULL, Param_Cell);
}

bool Extension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	char gamedata_error[256];
	if (!gameconfs->LoadGameConfigFile("voice", &g_pGameConf, gamedata_error, sizeof(gamedata_error)))
	{
		snprintf(error, maxlength, "Could not read voice.txt: %s", gamedata_error);
		return false;
	}

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	BroadcastVoiceDataDetour = DETOUR_CREATE_STATIC(BroadcastVoiceData, "SV_BroadcastVoiceData");
	if(BroadcastVoiceDataDetour == NULL) 
	{
		snprintf(error, maxlength, "Could not detour SV_BroadcastVoiceData");
		return false;
	}
	BroadcastVoiceDataDetour->EnableDetour();
	gameconfs->CloseGameConfigFile(g_pGameConf);
	return true;
}

void Extension::SDK_OnUnload()
{
	forwards->ReleaseForward(g_pBroadcastVoice);
	if(BroadcastVoiceDataDetour != NULL)
		BroadcastVoiceDataDetour->Destroy();
	
}
