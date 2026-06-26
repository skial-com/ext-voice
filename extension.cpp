#include "extension.h"
#include <iclient.h>
#include <dlfcn.h>
#include "symbol_resolve.h"

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Extension g_extension;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_extension);

// Resolve a mangled symbol from a loaded shared object. dlsym/memutils only see
// .dynsym (global exports); SV_BroadcastVoiceData is a *local* engine symbol, so
// fall back to an on-disk .symtab scan (ResolveSymtabSymbol) to find it.
static void* GetSigAddress(const char* lib, const char* sig)
{
	void* hLib = dlopen(lib, RTLD_LAZY);
	if (hLib != NULL)
	{
		dlclose(hLib);
		void* addr = memutils->ResolveSymbol(hLib, sig);
		if (addr) return addr;
	}
	return ResolveSymtabSymbol(lib, sig);
}

IForward *g_pBroadcastVoice;

// Engine: void SV_BroadcastVoiceData(IClient*, int nBytes, char *data, long long xuid)
// (mangled _Z21SV_BroadcastVoiceDataP7IClientiPcx -- the 4th arg is a long long
// passed by value, NOT a pointer; the old declaration's int64_t* was wrong and
// would corrupt the 32-bit stack layout.)
CDetour *BroadcastVoiceDataDetour;
void (*BroadcastVoiceData_Actual)(IClient* iclient, int nBytes, char *pVoiceData, long long xuid);
void BroadcastVoiceData(IClient* iclient, int nBytes, char *pVoiceData, long long xuid) {
	int client = iclient->GetPlayerSlot() + 1;
	if(g_pBroadcastVoice) {
		cell_t result = 0;
		g_pBroadcastVoice->PushCell(client);
		g_pBroadcastVoice->Execute(&result);
		if(result == Pl_Handled || result == Pl_Stop)
			return;
	}
	BroadcastVoiceData_Actual(iclient, nBytes, pVoiceData, xuid);
}

void Extension::SDK_OnAllLoaded()
{
	g_pBroadcastVoice = forwards->CreateForward("Skial_OnBroadcastVoice", ET_Hook, 1, NULL, Param_Cell);
}

bool Extension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	// The detour target is resolved directly by mangled symbol via
	// GetSigAddress(); this fork of CDetour has no gamedata path.
	CDetourManager::Init(g_pSM->GetScriptingEngine());

	void* addr = GetSigAddress("engine_srv.so", "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
	if(addr == NULL)
	{
		snprintf(error, maxlength, "Could not find SV_BroadcastVoiceData");
		return false;
	}

	BroadcastVoiceDataDetour = DETOUR_CREATE_STATIC(BroadcastVoiceData, addr);
	if(BroadcastVoiceDataDetour == NULL)
	{
		snprintf(error, maxlength, "Could not detour SV_BroadcastVoiceData");
		return false;
	}
	BroadcastVoiceDataDetour->EnableDetour();
	return true;
}

void Extension::SDK_OnUnload()
{
	forwards->ReleaseForward(g_pBroadcastVoice);
	if(BroadcastVoiceDataDetour != NULL)
		BroadcastVoiceDataDetour->Destroy();
	
}
