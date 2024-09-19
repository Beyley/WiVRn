/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>
#include <QMainWindow>
#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(wivrn_log_category)

namespace Ui
{
class MainWindow;
}
class IoGithubWivrnServerInterface;
class OrgFreedesktopDBusPropertiesInterface;

class settings;

class main_window : public QMainWindow
{
	Q_OBJECT

	Ui::MainWindow * ui = nullptr;
	IoGithubWivrnServerInterface * server = nullptr;
	OrgFreedesktopDBusPropertiesInterface * server_properties = nullptr;

	settings * settings_window = nullptr;

	// QIcon icon{":/images/wivrn.svg"};
	QIcon icon{":/assets/wivrn.png"};
	QSystemTrayIcon systray{icon};

	QMenu systray_menu;
	QAction action_show;
	QAction action_hide;
	QAction action_exit;

	QProcess * server_process;
	QTimer * server_process_timeout;

	QDBusServiceWatcher dbus_watcher;
	QDBusPendingCallWatcher * get_all_properties_call_watcher = nullptr;

	int mic_channels = 0;
	int mic_sample_rate = 0;
	int speakers_channels = 0;
	int speakers_sample_rate = 0;
	QString published_hostname;

	QString configuration;

public:
	main_window();
	~main_window();

	void changeEvent(QEvent * e) override;
	void closeEvent(QCloseEvent * event) override;
	void setVisible(bool visible) override;

	Q_PROPERTY(QString configuration READ get_configuration WRITE set_configuration)

	QString get_configuration()
	{
		return configuration;
	}
	void set_configuration(const QString & new_configuration);

private:
	void on_server_dbus_registered();
	void on_server_dbus_unregistered();
	void on_server_finished(int exit_code, QProcess::ExitStatus status);
	void on_server_error_occurred(QProcess::ProcessError error);
	void on_server_start_timeout();
	void on_server_properties_changed(const QString & interface_name, const QVariantMap & changed_properties, const QStringList & invalidated_properties);

	void on_action_settings();
	void start_server();
	void restart_server();
	void stop_server();
	void disconnect_client();

	void retranslate();
	void refresh_server_properties();
};
