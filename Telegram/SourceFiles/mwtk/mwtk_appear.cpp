/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_appear.h"

#include "history/history_item.h"
#include "mwtk/mwtk_options.h"
#include "ui/style/style_core_scale.h"

#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Mwtk::Appear {
namespace {

constexpr auto kDuration = crl::time(450);

[[nodiscard]] base::flat_map<FullMsgId, crl::time> &Items() {
	static auto result = base::flat_map<FullMsgId, crl::time>();
	return result;
}

} // namespace

void Register(not_null<HistoryItem*> item) {
	if (!EffectsEnabled() || !OptionValue(kOptionMessageAppear)) {
		return;
	}
	auto &items = Items();
	const auto now = crl::now();
	if (items.size() > 64) {
		for (auto i = items.begin(); i != items.end();) {
			if (now - i->second > kDuration) {
				i = items.erase(i);
			} else {
				++i;
			}
		}
	}
	items[item->fullId()] = now;
}

double Progress(not_null<const HistoryItem*> item) {
	auto &items = Items();
	if (items.empty()) {
		return -1.;
	}
	const auto i = items.find(item->fullId());
	if (i == items.end()) {
		return -1.;
	}
	const auto elapsed = crl::now() - i->second;
	if (elapsed >= kDuration) {
		items.erase(i);
		return -1.;
	}
	return std::clamp(elapsed / double(kDuration), 0., 1.);
}

bool Apply(QPainter &p, double progress, bool out, int width) {
	if (progress < 0. || progress >= 1.) {
		return false;
	}
	const auto eased = 1. - std::pow(1. - progress, 3.);
	const auto shift = (1. - eased) * 36. * style::Scale() / 100.;
	const auto scale = 0.94 + 0.06 * eased;
	const auto pivotX = out ? double(width) : 0.;
	p.setOpacity(p.opacity() * (0.15 + 0.85 * eased));
	p.translate(pivotX, shift);
	p.scale(scale, scale);
	p.translate(-pivotX, 0.);
	return true;
}

} // namespace Mwtk::Appear
