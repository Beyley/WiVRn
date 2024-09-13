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

#include "main_window.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QEvent>

#include <cmath>

#include "settings.h"
#include "ui_main_window.h"
#include "wivrn_server.h"

Q_LOGGING_CATEGORY(wivrn_log_category, "wivrn")

enum class server_state
{
	stopped = 0,
	listening,
	restarting,
	connected
};

Q_DECLARE_METATYPE(field_of_view)

const QDBusArgument & operator>>(const QDBusArgument & arg, field_of_view & fov)
{
	double left, right, up, down;
	arg.beginStructure();
	arg >> left >> right >> up >> down;
	arg.endStructure();

	fov.angleLeft = left;
	fov.angleRight = right;
	fov.angleUp = up;
	fov.angleDown = down;

	return arg;
}

QDebug operator<<(QDebug debug, const field_of_view & fov)
{
	QDebugStateSaver saver(debug);
	debug.nospace() << "Fov(" << fov.angleLeft << ", " << fov.angleRight << ", " << fov.angleUp << ", " << fov.angleDown << ")";
	return debug;
}

const QDBusArgument & operator>>(const QDBusArgument & arg, QSize & size)
{
	arg.beginStructure();
	arg >> size.rwidth() >> size.rheight();
	arg.endStructure();

	return arg;
}

main_window::main_window()
{
	ui = new Ui::MainWindow;
	ui->setupUi(this);

	if (QSystemTrayIcon::isSystemTrayAvailable())
	{
		action_show.setIcon(QIcon::fromTheme("settings-configure"));
		action_hide.setIcon(QIcon::fromTheme("settings-configure"));
		action_exit.setIcon(QIcon::fromTheme("application-exit"));

		systray_menu.addAction(&action_show);
		systray_menu.addAction(&action_hide);
		systray_menu.addAction(&action_exit);

		systray.setToolTip("WiVRn");
		systray.setContextMenu(&systray_menu);
		systray.show();

		connect(&action_show, &QAction::triggered, this, &QMainWindow::show);
		connect(&action_hide, &QAction::triggered, this, &QMainWindow::hide);
		connect(&action_exit, &QAction::triggered, this, [&]() { QApplication::quit(); });

		connect(&systray, &QSystemTrayIcon::activated, this, [&](QSystemTrayIcon::ActivationReason reason) {
			switch (reason)
			{
				case QSystemTrayIcon::ActivationReason::Trigger:
					setVisible(!isVisible());
					break;

				default:
					break;
			}
		});
	}

	dbus_watcher.setConnection(QDBusConnection::sessionBus());
	dbus_watcher.addWatchedService("io.github.wivrn.Server");
	connect(&dbus_watcher, &QDBusServiceWatcher::serviceRegistered, this, &main_window::on_server_started);
	connect(&dbus_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &main_window::on_server_finished);

	server_process.setProcessChannelMode(QProcess::ForwardedChannels);

	const QStringList services = QDBusConnection::sessionBus().interface()->registeredServiceNames();
	if (services.contains("io.github.wivrn.Server"))
		on_server_started();
	else
		ui->stackedWidget->setCurrentIndex((int)server_state::stopped);

	// connect(&server_process, &QProcess::started, this, &main_window::on_server_started);
	// connect(&server_process, &QProcess::finished, this, &main_window::on_server_finished);
	connect(&server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);

	connect(ui->action_start, &QAction::triggered, this, &main_window::start_server);
	connect(ui->action_stop, &QAction::triggered, this, &main_window::stop_server);
	connect(ui->action_restart, &QAction::triggered, this, &main_window::restart_server);

	connect(ui->button_start, &QPushButton::clicked, this, &main_window::start_server);
	connect(ui->button_stop, &QPushButton::clicked, this, &main_window::stop_server);
	connect(ui->button_settings, &QPushButton::clicked, this, &main_window::on_action_settings);
	connect(ui->button_restart, &QPushButton::clicked, this, &main_window::restart_server);
	connect(ui->button_disconnect, &QPushButton::clicked, this, &main_window::disconnect_client);

	connect(ui->action_settings, &QAction::triggered, this, &main_window::on_action_settings);

	connect(ui->action_about, &QAction::triggered, this, []() { QApplication::aboutQt(); });
	connect(ui->action_exit, &QAction::triggered, this, []() { QApplication::quit(); });

	connect(ui->copy_steam_command, &QPushButton::clicked, this, [&]() {
		QGuiApplication::clipboard()->setText(ui->label_steam_command->text());
	});

	connect(ui->copy_steam_command_2, &QPushButton::clicked, this, [&]() {
		QGuiApplication::clipboard()->setText(ui->label_steam_command->text());
	});

	retranslate();
}

main_window::~main_window()
{
	delete ui;
	ui = nullptr;
}

void main_window::changeEvent(QEvent * e)
{
	QWidget::changeEvent(e);

	switch (e->type())
	{
		case QEvent::LanguageChange:
			retranslate();
			break;

		default:
			break;
	}
}

void main_window::retranslate()
{
	ui->retranslateUi(this);

	action_show.setText(tr("&Show GUI"));
	action_hide.setText(tr("&Hide GUI"));
	action_exit.setText(tr("&Exit"));

	refresh_server_properties();
}

void main_window::closeEvent(QCloseEvent * event)
{
	QMainWindow::closeEvent(event);
	QCoreApplication::quit();
}

void main_window::setVisible(bool visible)
{
	if (visible)
	{
		action_show.setVisible(false);
		action_hide.setVisible(true);
	}
	else
	{
		action_show.setVisible(true);
		action_hide.setVisible(false);
	}

	QMainWindow::setVisible(visible);
}

void main_window::set_configuration(const QString & new_configuration)
{
	configuration = new_configuration;
	server->setJsonConfiguration(new_configuration);
}

void main_window::on_server_properties_changed(const QString & interface_name, const QVariantMap & changed_properties, const QStringList & invalidated_properties)
{
	if (interface_name == "io.github.wivrn.Server")
	{
		if (changed_properties.contains("RecommendedEyeSize"))
		{
			const auto arg = qvariant_cast<QDBusArgument>(changed_properties["RecommendedEyeSize"]);

			QSize eye_size;
			arg >> eye_size;

			// qDebug() << "Recommended eye size " << eye_size;
			// qDebug() << "Recommended eye size " << server->recommendedEyeSize(); // FIXME does not work
			// qDebug() << "Recommended eye size " << server->property("RecommendedEyeSize"); // FIXME does not work

			ui->label_eye_size->setText(tr("%1 \u2a2f %2").arg(eye_size.width()).arg(eye_size.height()));
		}

		if (changed_properties.contains("AvailableRefreshRates"))
		{
			const auto rates = qvariant_cast<QDBusArgument>(changed_properties["AvailableRefreshRates"]);

			QStringList rates_str;
			rates.beginArray();
			while (!rates.atEnd())
			{
				double element;
				rates >> element;
				// rates_str.push_back(QString::fromStdString(std::format(tr("{} Hz", nullptr).toStdString(), element)));
				rates_str.push_back(tr("%1 Hz").arg(element));
			}
			rates.endArray();

			ui->label_refresh_rates->setText(rates_str.join(", "));
		}

		if (changed_properties.contains("PreferredRefreshRate"))
			ui->label_prefered_refresh_rate->setText(tr("%1 Hz").arg(changed_properties["PreferredRefreshRate"].toInt()));

		if (changed_properties.contains("EyeGaze"))
		{
			if (changed_properties["EyeGaze"].toBool())
				ui->label_eye_gaze_tracking->setText(tr("Supported"));
			else
				ui->label_eye_gaze_tracking->setText(tr("Not supported"));
		}

		if (changed_properties.contains("FaceTracking"))
		{
			if (changed_properties["FaceTracking"].toBool())
				ui->label_face_tracking->setText(tr("Supported"));
			else
				ui->label_face_tracking->setText(tr("Not supported"));
		}

		if (changed_properties.contains("FieldOfView"))
		{
			const auto fovs = qvariant_cast<QDBusArgument>(changed_properties["FieldOfView"]);
			fovs.beginArray();

			QStringList fov_str;
			std::vector<field_of_view> fovs2;
			while (!fovs.atEnd())
			{
				field_of_view fov;
				fovs >> fov;
				fovs2.push_back(fov);

				// fov_str.push_back(std::format("({:.1f}° \u2012 {:.1f}°) \u2a2f ({:.1f}° \u2012 {:.1f}°)",
				//                               fov.angleLeft * 180 / M_PI,
				//                               fov.angleRight * 180 / M_PI,
				//                               fov.angleDown * 180 / M_PI,
				//                               fov.angleUp * 180 / M_PI)
				//                           .c_str());

				// fov_str.push_back(tr("%1° \u2a2f %2°").arg((fov.angleRight - fov.angleLeft) * 180 / M_PI, 0, 'f', 1).arg((fov.angleUp - fov.angleDown) * 180 / M_PI, 0, 'f', 1));
			}
			fovs.endArray();

			if (fovs2.size() >= 2)
			{
				ui->label_field_of_view->setText(tr("Left eye: %1° \u2a2f %2°, right eye: %3° \u2a2f %4°")
				                                         .arg((fovs2[0].angleRight - fovs2[0].angleLeft) * 180 / M_PI, 0, 'f', 1)
				                                         .arg((fovs2[0].angleUp - fovs2[0].angleDown) * 180 / M_PI, 0, 'f', 1)
				                                         .arg((fovs2[1].angleRight - fovs2[1].angleLeft) * 180 / M_PI, 0, 'f', 1)
				                                         .arg((fovs2[1].angleUp - fovs2[1].angleDown) * 180 / M_PI, 0, 'f', 1));
			}

			// ui->label_field_of_view->setText(fov_str.join(", "));
		}

		if (changed_properties.contains("HandTracking"))
		{
			if (changed_properties["HandTracking"].toBool())
				ui->label_hand_tracking->setText(tr("Supported"));
			else
				ui->label_hand_tracking->setText(tr("Not supported"));
		}

		if (changed_properties.contains("HeadsetConnected"))
		{
			bool connected = changed_properties["HeadsetConnected"].toBool();

			if (connected)
				ui->stackedWidget->setCurrentIndex((int)server_state::connected);
			else
				ui->stackedWidget->setCurrentIndex((int)server_state::listening);
		}

		if (changed_properties.contains("JsonConfiguration"))
		{
			configuration = changed_properties["JsonConfiguration"].toString();
		}

		if (changed_properties.contains("MicChannels"))
			mic_channels = changed_properties["MicChannels"].toInt();

		if (changed_properties.contains("MicSampleRate"))
			mic_sample_rate = changed_properties["MicSampleRate"].toInt();

		if (changed_properties.contains("MicChannels") or changed_properties.contains("MicSampleRate"))
		{
			if (mic_channels)
				ui->label_mic->setText(tr("%n channel(s), %1 Hz", nullptr, mic_channels).arg(mic_sample_rate));
			else
				ui->label_mic->setText(tr("N/A"));
		}

		if (changed_properties.contains("SpeakerChannels"))
			speakers_channels = changed_properties["SpeakerChannels"].toInt();

		if (changed_properties.contains("SpeakerSampleRate"))
			speakers_sample_rate = changed_properties["SpeakerSampleRate"].toInt();

		if (changed_properties.contains("SpeakerChannels") or changed_properties.contains("SpeakerSampleRate"))
		{
			if (speakers_channels)
				ui->label_speaker->setText(tr("%n channel(s), %1 Hz", nullptr, speakers_channels).arg(speakers_sample_rate));
			else
				ui->label_speaker->setText(tr("N/A"));
		}

		if (changed_properties.contains("SteamCommand"))
		{
			ui->label_steam_command->setText(changed_properties["SteamCommand"].toString());
			ui->label_steam_command_2->setText(changed_properties["SteamCommand"].toString());
		}

		if (changed_properties.contains("SupportedCodecs"))
			ui->label_codecs->setText(changed_properties["SupportedCodecs"].toStringList().join(", "));
	}
}

void main_window::refresh_server_properties()
{
	if (!server_properties)
		return;

	QDBusPendingReply<QVariantMap> props_pending = server_properties->GetAll("io.github.wivrn.Server");

	if (get_all_properties_call_watcher)
		get_all_properties_call_watcher->deleteLater();
	get_all_properties_call_watcher = new QDBusPendingCallWatcher{props_pending, this};

	connect(get_all_properties_call_watcher, &QDBusPendingCallWatcher::finished, [this, props_pending]() {
		on_server_properties_changed("io.github.wivrn.Server", props_pending.value(), {});
	});
}

void main_window::on_server_started()
{
	if (server)
		server->deleteLater();
	if (server_properties)
		server_properties->deleteLater();

	server = new IoGithubWivrnServerInterface("io.github.wivrn.Server", "/io/github/wivrn/Server", QDBusConnection::sessionBus(), this);
	server_properties = new OrgFreedesktopDBusPropertiesInterface("io.github.wivrn.Server", "/io/github/wivrn/Server", QDBusConnection::sessionBus(), this);

	connect(server_properties, &OrgFreedesktopDBusPropertiesInterface::PropertiesChanged, this, &main_window::on_server_properties_changed);

	refresh_server_properties();

	if (ui)
	{
		if (server->headsetConnected())
		{
			ui->stackedWidget->setCurrentIndex((int)server_state::connected);
		}
		else
		{
			ui->stackedWidget->setCurrentIndex((int)server_state::listening);
		}

		ui->action_start->setEnabled(false);
		ui->action_restart->setEnabled(true);
		ui->action_stop->setEnabled(true);
		ui->action_settings->setEnabled(true);

		ui->button_stop->setEnabled(true);
		ui->button_restart->setEnabled(true);

		ui->label_steam_command->setText(server->steamCommand());
	}
}

void main_window::on_server_finished()
{
	if (server)
		server->deleteLater();
	if (server_properties)
		server_properties->deleteLater();

	server = nullptr;
	server_properties = nullptr;

	if (ui)
	{
		if (ui->stackedWidget->currentIndex() == (int)server_state::restarting)
			start_server();
		else
			ui->stackedWidget->setCurrentIndex((int)server_state::stopped);

		ui->action_start->setEnabled(true);
		ui->action_restart->setEnabled(false);
		ui->action_stop->setEnabled(false);
		ui->action_settings->setEnabled(false);

		ui->button_start->setEnabled(true);
	}
}

void main_window::on_server_error_occurred(QProcess::ProcessError error)
{
	if (ui)
	{
		ui->stackedWidget->setCurrentIndex((int)server_state::stopped);
		ui->action_start->setEnabled(true);
		ui->action_restart->setEnabled(false);
		ui->action_stop->setEnabled(false);
		ui->action_settings->setEnabled(false);
	}
}

void main_window::on_action_settings()
{
	if (settings_window)
	{
		settings_window->activateWindow();
	}
	else
	{
		settings_window = new settings(this);
		settings_window->show();

		connect(settings_window, &QDialog::finished, [&](int r) {
			settings_window->deleteLater();
			settings_window = nullptr;
		});
	}
}

void main_window::start_server()
{
	// TODO activate by dbus?
	server_process.start(QCoreApplication::applicationDirPath() + "/wivrn-server");
	ui->button_start->setEnabled(false);
	ui->action_start->setEnabled(false);
}

void main_window::stop_server()
{
	if (server)
		server->Quit() /*.waitForFinished()*/;
	// server_process.terminate(); // TODO timer and kill
	ui->button_stop->setEnabled(false);
	ui->button_restart->setEnabled(false);
	ui->action_stop->setEnabled(false);
	ui->action_restart->setEnabled(false);
}

void main_window::restart_server()
{
	ui->stackedWidget->setCurrentIndex((int)server_state::restarting);
	stop_server();
}

void main_window::disconnect_client()
{
	if (server)
		server->Disconnect();
}

#include "moc_main_window.cpp"
