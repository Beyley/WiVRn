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

#include <QApplication>
#include <QTranslator>

#include "main_window.h"

namespace xrt::drivers::wivrn
{
extern const char git_version[];
}

int main(int argc, char * argv[])
{
	QApplication app(argc, argv);

	app.setApplicationDisplayName("WiVRn");
	app.setApplicationName("wivrn-gui");
	app.setOrganizationName("wivrn");
	app.setApplicationVersion(xrt::drivers::wivrn::git_version);
	app.setDesktopFileName("io.github.wivrn.wivrn");

	QTranslator translator;

	QLocale locale = QLocale::system();
	if (translator.load(locale, "wivrn", "_", ":/i18n"))
	{
		qDebug() << "Adding translator for " << locale;
		app.installTranslator(&translator);
	}
	else
	{
		qDebug() << "Cannot add translator for " << locale;
	}

	main_window window;
	window.show();

	return app.exec();
}
