/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mwtk/mwtk_scene.h"
#include "ui/rp_widget.h"

namespace Mwtk {

class VisualsWidget final : public Ui::RpWidget {
public:
	VisualsWidget(QWidget *parent, bool followOptions);

	void setConfig(const SceneConfig &config);
	void setRunning(bool running);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void tick();
	void refreshFromOptions();
	[[nodiscard]] bool canAnimate() const;

	Scene _scene;
	base::Timer _timer;
	crl::time _lastTick = 0;
	bool _running = false;
	bool _followOptions = false;

};

void SetupOverlay(not_null<Ui::RpWidget*> body);

} // namespace Mwtk
