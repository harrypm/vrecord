/* Windows declaration of the factory the Linux/Mac SDK provides via
 * DeckLinkAPIDispatch.cpp; the Windows COM-style DeckLinkAPI.h omits it. */
#ifndef DECKLINK_DISPATCH_WIN_H
#define DECKLINK_DISPATCH_WIN_H
#include "DeckLinkAPI.h"
extern IDeckLinkIterator *CreateDeckLinkIteratorInstance(void);
#endif
