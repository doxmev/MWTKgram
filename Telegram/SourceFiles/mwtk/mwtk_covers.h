/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::CustomCovers {

// Your own cover for a song. Kept only on this computer, by artist and
// title, so the same song in any chat gets it.

[[nodiscard]] QImage Get(DocumentData *document);
[[nodiscard]] bool Has(DocumentData *document);
void Pick(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document);
void Remove(not_null<DocumentData*> document);
[[nodiscard]] rpl::producer<> Changes();

} // namespace Mwtk::CustomCovers
