#include "qdosview.h"

#include "machine.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

QdosView::QdosView(QQuickItem* parent) : QQuickPaintedItem(parent) {
	setOpaquePainting(false);
	setAcceptedMouseButtons(Qt::LeftButton);
	setFlag(ItemAcceptsInputMethod, false);
}

void QdosView::setMachine(Machine* machine) {
	if (m_machine == machine) {
		return;
	}
	if (m_machine != nullptr) {
		disconnect(m_machine, nullptr, this, nullptr);
	}
	m_machine = machine;
	if (m_machine != nullptr) {
		connect(m_machine, &Machine::frameChanged, this, &QdosView::frameChanged);
	}
	frameChanged();
	emit machineChanged();
}

void QdosView::frameChanged() {
	m_scaled = QImage();
	m_scaledBy = 0;
	update();
}

void QdosView::paint(QPainter* painter) {
	const QImage frame = m_machine != nullptr ? m_machine->frame() : QImage();
	if (frame.isNull() || width() <= 0 || height() <= 0) {
		return;
	}

	// Fitted whole and centred, as SDL's letterbox does on Android
	const QRect src = frame.rect();
	const qreal scale = std::min(width() / src.width(), height() / src.height());
	const QSizeF size(src.width() * scale, src.height() * scale);
	m_target = QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);

	// A phone is rarely a whole multiple of the case. Nearest-neighbour at a
	// fraction would draw some of the font's 2px stems 3px wide, and smoothing
	// the whole way would blur them, so the frame is blown up to the whole
	// multiple above and smoothed down from there: SDL's pixel-art filter, near
	// enough.
	const int by = std::max(1, (int)std::ceil(scale));
	if (m_scaled.isNull() || m_scaledBy != by) {
		m_scaled = frame.scaled(src.size() * by, Qt::IgnoreAspectRatio, Qt::FastTransformation);
		m_scaledBy = by;
	}

	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->drawImage(m_target, m_scaled);
}

void QdosView::mousePressEvent(QMouseEvent* event) {
	if (m_machine == nullptr || m_target.isEmpty()) {
		event->ignore();
		return;
	}

	forceActiveFocus();
	// A key is a key: nothing underneath gets to turn it into a swipe
	setKeepMouseGrab(true);

	const QSize src(QDOS_WINDOW_W, QDOS_WINDOW_H);
	const int x = (int)std::floor((event->localPos().x() - m_target.left()) * src.width() / m_target.width());
	const int y = (int)std::floor((event->localPos().y() - m_target.top()) * src.height() / m_target.height());
	m_machine->press(x, y);
	m_down = true;
	event->accept();
}

void QdosView::mouseReleaseEvent(QMouseEvent* event) {
	mouseUngrabEvent();
	event->accept();
}

void QdosView::mouseUngrabEvent() {
	setKeepMouseGrab(false);
	if (m_down && m_machine != nullptr) {
		m_machine->release();
	}
	m_down = false;
}

void QdosView::keyPressEvent(QKeyEvent* event) {
	if (m_machine == nullptr) {
		event->ignore();
		return;
	}

	switch (event->key()) {
	case Qt::Key_Return:
	case Qt::Key_Enter:
		m_machine->key(QDOS_KEY_ENTER);
		break;
	case Qt::Key_Backspace:
		m_machine->key(QDOS_KEY_BACKSPACE);
		break;
	case Qt::Key_Tab:
		m_machine->key(QDOS_KEY_TAB);
		break;
	case Qt::Key_Up:
		m_machine->key(QDOS_KEY_UP);
		break;
	case Qt::Key_Down:
		m_machine->key(QDOS_KEY_DOWN);
		break;
	case Qt::Key_Left:
		m_machine->key(QDOS_KEY_LEFT);
		break;
	case Qt::Key_Right:
		m_machine->key(QDOS_KEY_RIGHT);
		break;
	case Qt::Key_F1:
	case Qt::Key_F2:
	case Qt::Key_F3:
	case Qt::Key_F4:
	case Qt::Key_F5:
		m_machine->key((qdos_key)(QDOS_KEY_SOFT1 + (event->key() - Qt::Key_F1)));
		break;
	case Qt::Key_Escape:
		m_machine->key(QDOS_KEY_CLEAR);
		break;
	case Qt::Key_F10:
		m_machine->key(QDOS_KEY_POWER);
		break;
	default: {
		// The shell's line is ASCII, as a keyboard on the simulator types it
		const QString text = event->text();
		if (text.size() != 1 || text[0].unicode() < 0x20 || text[0].unicode() > 0x7E) {
			event->ignore();
			return;
		}
		m_machine->typed((char)text[0].unicode());
		break;
	}
	}
	event->accept();
}
