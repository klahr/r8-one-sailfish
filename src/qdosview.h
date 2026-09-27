/*
 * The calculator on screen: the machine's frame, scaled to the item, with
 * touches mapped back to the window pixels the keypad is laid out in.
 */

#ifndef R8_QDOSVIEW_H
#define R8_QDOSVIEW_H

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>

#include "machine.h"

class QdosView : public QQuickPaintedItem {
	Q_OBJECT

	Q_PROPERTY(Machine* machine READ machine WRITE setMachine NOTIFY machineChanged)

public:
	explicit QdosView(QQuickItem* parent = nullptr);

	Machine* machine() const { return m_machine; }
	void setMachine(Machine* machine);

	void paint(QPainter* painter) override;

signals:
	void machineChanged();

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseUngrabEvent() override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	void frameChanged();

	QPointer<Machine> m_machine;
	bool m_down = false;

	/** Where the frame last landed in the item */
	QRectF m_target;

	/** The frame blown up to a whole multiple of itself, for the smooth pass */
	QImage m_scaled;
	int m_scaledBy = 0;
};

#endif // R8_QDOSVIEW_H
