/*
 * Windows replacement for the SDK's Linux-only DeckLinkAPIDispatch.cpp.
 * deckcontrol only needs CreateDeckLinkIteratorInstance(); on Windows the
 * DeckLink API is a COM server, so hand out the iterator via CoCreateInstance.
 */
#include <windows.h>
#include <objbase.h>
#include "DeckLinkAPIDispatch_win.h"
#include "DeckLinkAPI_i.c" /* CLSID/IID GUID definitions (single TU only) */

IDeckLinkIterator *CreateDeckLinkIteratorInstance(void)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	IDeckLinkIterator *iterator = NULL;
	HRESULT hr = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL,
		IID_IDeckLinkIterator, (void **)&iterator);
	if (FAILED(hr))
		return NULL;
	return iterator;
}
