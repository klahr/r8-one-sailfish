/*
 * The calculator: QDOS's shell on a thread of its own, behind a HAL that takes
 * its keys from the view and hands its frames back to it.
 *
 * The shell is a loop that blocks between keys, as it does on the hardware, so
 * it cannot run on the GUI thread. Everything it sees arrives through one
 * queue, and everything it shows leaves as a QImage.
 */

#ifndef R8_MACHINE_H
#define R8_MACHINE_H

#include <QImage>
#include <QObject>
#include <QString>

#include <qdos/hal.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "hal/sim/keypad_ui.h"

class Machine : public QObject {
	Q_OBJECT

	/** The case, panel and keypad, QDOS_WINDOW_W by QDOS_WINDOW_H */
	Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)

	/** The top of the stack as the display shows it, and how deep the stack is */
	Q_PROPERTY(QString top READ top NOTIFY topChanged)
	Q_PROPERTY(int depth READ depth NOTIFY topChanged)

public:
	/** The stores, as the Android activity sets them up before SDL starts */
	struct Stores {
		QString system;
		QString user;
		QString inbox;
		QString nativeCache;
	};

	explicit Machine(const Stores& stores, QObject* parent = nullptr);
	~Machine() override;

	QImage frame() const { return m_frame; }
	QString top() const { return m_top; }
	int depth() const { return m_depth; }

	/** Starts the shell; it runs until it powers off or stop() is called */
	void start();

	/** Asks the shell to save and return, and waits for it */
	void stop();

	/** A touch at a window coordinate, case included */
	void press(int x, int y);
	void release();

	/** A key from a hardware keyboard */
	void key(qdos_key k);
	void typed(char ch);

	/** The window is being hidden: save now, in case we are killed there */
	Q_INVOKABLE void save();

signals:
	void frameChanged();
	void topChanged();

	/** The shell returned by itself: the power key, or auto-off */
	void poweredOff();

	/** Emitted from the shell thread, so it crosses to the GUI queued */
	void frameReady(const QImage& image);
	void topReady(const QString& top, int depth);

private:
	enum class Kind { Press, Release, Key, Char, Save, Wake };

	struct Event {
		Kind kind;
		int x;
		int y;
		qdos_key key;
		char ch;
	};

	void post(const Event& ev);
	void run();

	// The HAL entry points, on the shell thread
	static int halInit(qdos_hal* hal);
	static void halShutdown(qdos_hal* hal);
	static void halPresent(qdos_hal* hal, const uint8_t* fb);
	static bool halPollKey(qdos_hal* hal, qdos_key_event* out);
	static qdos_keypad_mod halModifier(qdos_hal* hal);
	static bool halRunning(qdos_hal* hal);
	static uint32_t halTicks(qdos_hal* hal);
	static void halWait(qdos_hal* hal, int timeout_ms);
	static qdos_store_result halStoreRead(
			qdos_hal* hal, qdos_store_scope scope, const char* name, void* buf, size_t cap, size_t* len);
	static qdos_store_result halStoreWrite(qdos_hal* hal, const char* name, const void* buf, size_t len);
	static qdos_store_result halStoreRemove(qdos_hal* hal, const char* name);
	static qdos_store_result halStoreList(
			qdos_hal* hal, qdos_store_scope scope, const char* folder, qdos_store_visit visit, void* user);
	static bool halStorePath(qdos_hal* hal, qdos_store_scope scope, const char* name, char* buf, size_t cap);
	static bool halStoreChanged(qdos_hal* hal);
	static bool halTimeOfDay(qdos_hal* hal, int* seconds);
	static int halBattery(qdos_hal* hal);
	static void halShowTop(qdos_hal* hal, const char* text, size_t depth);

	static Machine* self(qdos_hal* hal) { return static_cast<Machine*>(hal->impl); }

	const char* dirFor(qdos_store_scope scope) const;
	void pushFrame();
	void watchInbox();
	void watchThread();

	QImage m_frame;
	QString m_top;
	int m_depth = 0;

	QByteArray m_system;
	QByteArray m_user;
	QByteArray m_inbox;
	QByteArray m_battery; ///< Where the phone's own charge is read, if anywhere

	qdos_hal m_hal;
	std::chrono::steady_clock::time_point m_start;
	std::thread m_shell;

	std::mutex m_lock;
	std::condition_variable m_wake;
	std::deque<Event> m_events; ///< Guarded by m_lock
	bool m_running = false;		///< Guarded by m_lock

	// The shell thread's own from here on
	const char* m_pending = nullptr; ///< Rest of a text button still to be delivered
	qdos_pad_layer m_layer = QDOS_PAD_PLAIN;
	qdos_pad_layer m_locked = QDOS_PAD_PLAIN;
	const qdos_pad_button* m_pressed = nullptr;
	QByteArray m_shownTop; ///< What was last sent, so a repaint of the same sends nothing
	size_t m_shownDepth = 0;
	uint8_t m_rgb[QDOS_WINDOW_W * QDOS_WINDOW_H * 3];

	/*
	 * inotify, on a thread that posts a wake to the queue: the shell sleeps
	 * with no timer, and polling the inbox would cost the wakeups that buys.
	 */
	int m_watchFd = -1;
	int m_watchId = -1;
	int m_stopFd[2] = {-1, -1};
	int m_subId[16];
	size_t m_subCount = 0;
	std::thread m_watcher;
	std::atomic<bool> m_storeDirty{false};
};

#endif // R8_MACHINE_H
