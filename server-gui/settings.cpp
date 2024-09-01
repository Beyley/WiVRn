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

#include "settings.h"

#include "main_window.h"
#include "rectangle_partitionner.h"
#include "ui_settings.h"

#include <QFile>
#include <QJsonObject>
#include <QStandardPaths>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Must match the order in the combo boxes
const std::vector<std::pair<int, std::string>> encoder_ids{
        {0, "auto"},
        {1, "nvenc"},
        {2, "vaapi"},
        {3, "x264"},
};

const std::vector<std::pair<int, std::string>> codec_ids{
        {0, "auto"},
        {1, "h264"},
        {1, "avc"},
        {2, "h265"},
        {3, "av1"},
};

int encoder_id_from_string(std::string_view s)
{
	for (auto & [i, j]: encoder_ids)
	{
		if (j == s)
			return i;
	}
	return 0;
}

const std::string & encoder_from_id(int id)
{
	for (auto & [i, j]: encoder_ids)
	{
		if (i == id)
			return j;
	}

	static const std::string default_value = "auto";
	return default_value;
}

int codec_id_from_string(std::string_view s)
{
	for (auto & [i, j]: codec_ids)
	{
		if (j == s)
			return i;
	}
	return 0;
}

const std::string & codec_from_id(int id)
{
	for (auto & [i, j]: codec_ids)
	{
		if (i == id)
			return j;
	}

	static const std::string default_value = "auto";
	return default_value;
}

} // namespace

settings::settings(main_window * parent) :
        parent(parent)
{
	ui = new Ui::Settings;
	ui->setupUi(this);

	QJsonParseError error;
	json_doc = QJsonDocument::fromJson(parent->get_configuration().toUtf8(), &error);

	QList<QRectF> rectangles;
	QList<QVariant> encoder_config;

	if (error.error)
	{
		std::cout << "Cannot read configuration: " << error.errorString().toStdString() << ", offset " << error.offset << std::endl;
	}
	else
	{
		for (QJsonValue i: json_doc["encoders"].toArray())
		{
			int encoder = encoder_id_from_string(i["encoder"].toString("auto").toLower().toStdString());
			int codec = codec_id_from_string(i["codec"].toString("auto").toLower().toStdString());
			double width = i["width"].toDouble(1);
			double height = i["height"].toDouble(1);
			double offset_x = i["offset_x"].toDouble(0);
			double offset_y = i["offset_y"].toDouble(0);
			int group = i["group"].toInt(0); // TODO: handle groups

			rectangles.push_back(QRectF(offset_x, offset_y, width, height));
			encoder_config.push_back(QVariant::fromValue(std::pair(encoder, codec)));
		}
	}

	if (rectangles.empty())
	{
		rectangles.push_back(QRectF(0, 0, 1, 1));
		std::pair<std::string, std::string> config{"auto", "auto"};
		encoder_config.push_back(QVariant::fromValue(config));
	}

	ui->partitionner->set_rectangles(rectangles);
	ui->partitionner->set_rectangles_data(encoder_config);
	ui->partitionner->set_selected_index(0);

	ui->scale->setValue(json_doc["scale"].toDouble(1) * 100);
	ui->bitrate->setValue(json_doc["bitrate"].toDouble(50'000'000) / 1'000'000);

	selected_rectangle_changed(0);

	connect(ui->partitionner, &rectangle_partitionner::selected_index_change, this, &settings::selected_rectangle_changed);
	// connect(ui->partitionner, &rectangle_partitionner::rectangles_change, this, [](){});

	connect(ui->encoder, &QComboBox::currentIndexChanged, this, &settings::on_settings_changed);
	connect(ui->codec, &QComboBox::currentIndexChanged, this, &settings::on_settings_changed);
	connect(ui->bitrate, &QDoubleSpinBox::valueChanged, this, &settings::on_settings_changed);
	connect(ui->scale, &QDoubleSpinBox::valueChanged, this, &settings::on_settings_changed);

	connect(this, &QDialog::accepted, this, &settings::save_settings);
}

settings::~settings()
{
	delete ui;
	ui = nullptr;
}

void settings::on_settings_changed()
{
	QString status;

	int encoder = ui->encoder->currentIndex();
	int codec = ui->codec->currentIndex();

	if (encoder == 3 /* x264 */ and codec != 1 /* H264 */ and codec != 0 /* auto */)
	{
		status += tr("x264 only supports H264\n");
	}

	if (codec == 3 /* av1 */)
	{
		status += tr("Not all headsets support AV1\n");
	}

	ui->partitionner->set_rectangles_data(ui->partitionner->selected_index(), QVariant::fromValue(std::pair(encoder, codec)));

	ui->message->setText(status);
}

void settings::selected_rectangle_changed(int index)
{
	auto [encoder, codec] = ui->partitionner->rectangles_data(index).value<std::pair<int, int>>();

	ui->encoder->setCurrentIndex(encoder);
	ui->codec->setCurrentIndex(codec);
}

void settings::save_settings()
{
	QJsonObject json = json_doc.object();
	json["scale"] = ui->scale->value() / 100;
	json["bitrate"] = ui->bitrate->value() * 1'000'000;

	QJsonArray encoders;

	const auto & rect = ui->partitionner->rectangles();
	const auto & data = ui->partitionner->rectangles_data();

	for (int i = 0, n = rect.size(); i < n; i++)
	{
		QJsonObject encoder;
		auto [encoder_id, codec_id] = data[i].value<std::pair<int, int>>();

		encoder["encoder"] = QString::fromLatin1(encoder_from_id(encoder_id));
		encoder["codec"] = QString::fromLatin1(codec_from_id(codec_id));
		encoder["width"] = rect[i].width();
		encoder["height"] = rect[i].height();
		encoder["offset_x"] = rect[i].x();
		encoder["offset_y"] = rect[i].y();

		// TODO encoder groups

		encoders.push_back(encoder);
	}

	json["encoders"] = encoders;

	parent->set_configuration(QString::fromUtf8(QJsonDocument(json).toJson()));
}

#include "moc_settings.cpp"
