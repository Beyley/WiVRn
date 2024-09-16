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

#include "rectangle_partitionner.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionFocusRect>

#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>

namespace
{
enum class side
{
	top,
	bottom,
	left,
	right
};

struct edge
{
	side edge_side;
	int rectangle_index;

	bool operator==(const edge & other) const noexcept
	{
		return edge_side == other.edge_side and rectangle_index == other.rectangle_index;
	}

	int position = 0;
	int min = std::numeric_limits<int>::max();
	int max = std::numeric_limits<int>::lowest();
};
} // namespace

struct rectangle_partitionner_private
{
	std::vector<edge> selection;
	std::vector<edge> hovered;

	int min_drag_position;
	int max_drag_position;

	QLine split_line;

	enum class mouse_event_type
	{
		move,
		press,
		release
	};

	// Used for replay
	std::vector<std::pair<mouse_event_type, QMouseEvent *>> events;
	QSize initial_size;

	std::vector<QRectF> rectangles_float;
	std::vector<QRect> rectangles_int;

	rectangle_partitionner_private() = default;
	rectangle_partitionner_private(const rectangle_partitionner_private &) = delete;
	rectangle_partitionner_private & operator=(const rectangle_partitionner_private &) = delete;
	~rectangle_partitionner_private()
	{
		for (auto [i, j]: events)
		{
			delete j;
		}
	}
};

namespace
{
int hovered_rectangle(QPoint position, std::vector<QRect> rectangles)
{
	int n = 0;
	for (const QRect & i: rectangles)
	{
		if (i.contains(position))
			return n;
		n++;
	}

	return -1;
}

Qt::CursorShape get_cursor_shape(QPointF position, QRect bounding_box, std::vector<QRect> rectangles, int margin_move_edges, int margin_split_edges)
{
	bounding_box.adjust(margin_move_edges, margin_move_edges, -margin_move_edges, -margin_move_edges);

	for (QRect & i: rectangles)
	{
		if (i.contains(position.toPoint()))
		{
			QPoint rel_pos = position.toPoint() - i.topLeft();
			double dx = std::min(rel_pos.x(), i.width() - rel_pos.x());
			double dy = std::min(rel_pos.y(), i.height() - rel_pos.y());

			if (dx < margin_move_edges and position.x() >= bounding_box.left() and position.x() <= bounding_box.right())
				return Qt::SizeHorCursor;
			else if (dy < margin_move_edges and position.y() >= bounding_box.top() and position.y() <= bounding_box.bottom())
				return Qt::SizeVerCursor;
			else if (dx < margin_split_edges)
				return Qt::SplitVCursor;
			else if (dy < margin_split_edges)
				return Qt::SplitHCursor;
			else
				return Qt::ArrowCursor;

			break;
		}
	}

	return Qt::ArrowCursor;
}

std::vector<edge> horizontal_edges(QRect bounding_box, std::vector<QRect> rectangles)
{
	std::vector<edge> edges;

	for (int i = 0, n = rectangles.size(); i < n; i++)
	{
		if (rectangles[i].top() > bounding_box.top())
		{
			edges.emplace_back(side::top, i, rectangles[i].top(), rectangles[i].left(), rectangles[i].right());
		}

		if (rectangles[i].bottom() < bounding_box.bottom())
		{
			edges.emplace_back(side::bottom, i, rectangles[i].bottom(), rectangles[i].left(), rectangles[i].right());
		}
	}

	return edges;
}

std::vector<edge> vertical_edges(QRect bounding_box, std::vector<QRect> rectangles)
{
	std::vector<edge> edges;

	for (int i = 0, n = rectangles.size(); i < n; i++)
	{
		if (rectangles[i].left() > bounding_box.left())
		{
			edges.emplace_back(side::left, i, rectangles[i].left(), rectangles[i].top(), rectangles[i].bottom());
		}

		if (rectangles[i].right() < bounding_box.right())
		{
			edges.emplace_back(side::right, i, rectangles[i].right(), rectangles[i].top(), rectangles[i].bottom());
		}
	}

	return edges;
}

std::vector<std::vector<edge>> partition_edges(std::vector<edge> edges)
{
	auto comp = [](const edge & a, const edge & b) {
		if (a.position < b.position)
			return true;
		if (a.position > b.position)
			return false;

		if (a.min < b.min)
			return true;
		if (a.min > b.min)
			return false;

		if (a.max < b.max)
			return true;
		return false;
	};

	std::ranges::sort(edges, comp);

	std::vector<std::vector<edge>> partitionned;

	int current_position = std::numeric_limits<int>::lowest();
	int current_max = 0;

	for (auto & e: edges)
	{
		if (e.position != current_position)
		{
			current_position = e.position;
			current_max = e.max;
			partitionned.push_back({e});
		}
		else
		{
			if (e.min >= current_max)
			{
				// All the following edges will be after the last partition
				current_max = e.max;
				partitionned.push_back({e});
			}
			else
			{
				// Add the edge to the current partition
				current_max = std::max(current_max, e.max);
				partitionned.back().push_back(e);
			}
		}
	}

	return partitionned;
}

std::vector<edge> hovered_edges(QPointF position, const std::vector<std::vector<edge>> & partitionned_edges, bool horizontal)
{
	if (horizontal)
		position = position.transposed();

	for (const auto & edge_list: partitionned_edges)
	{
		for (const auto & edge: edge_list)
		{
			// TODO configurable margin
			if (std::abs(position.x() - edge.position) < 5 and position.y() >= edge.min and position.y() <= edge.max)
			{
				return edge_list;
			}
		}
	}

	return {};
}

bool assert_rectangle_list_is_partition(QRect bounding_box, const std::vector<QRect> & rectangles)
{
	bool ok = true;

	for (const auto & i: rectangles)
		if (not i.isValid())
		{
			qDebug() << "Invalid rectangle" << i;
			ok = false;
		}

	for (int y = bounding_box.top(); y < bounding_box.bottom(); ++y)
	{
		std::vector<std::pair<int, int>> horizontal_segments;
		for (const auto & i: rectangles)
		{
			if (y >= i.top() and y < i.bottom())
				horizontal_segments.emplace_back(i.left(), i.right());
		}

		std::ranges::sort(horizontal_segments);

		if (horizontal_segments.empty())
		{
			qDebug() << "Line" << y << "empty";
			ok = false;
		}

		if (horizontal_segments.front().first != bounding_box.left())
		{
			qDebug() << "Line" << y << "does not start at the left of the bounding box";
			ok = false;
		}

		if (horizontal_segments.back().second != bounding_box.right())
		{
			qDebug() << "Last rectangle of line" << y << "does not end at the right of the bounding box";
			ok = false;
		}

		for (int i = 1; i < horizontal_segments.size(); ++i)
		{
			if (horizontal_segments[i - 1].second < horizontal_segments[i].first)
			{
				qDebug() << "Overlap in line" << y;
				ok = false;
			}
			else if (horizontal_segments[i - 1].second > horizontal_segments[i].first)
			{
				qDebug() << "Gap in line" << y;
				ok = false;
			}
		}
	}

	if (!ok)
	{
		qDebug() << "Bounding box:" << bounding_box;
		int n = 1;
		for (auto & i: rectangles)
		{
			qDebug() << "Rectangle" << (n++) << ":" << i.topLeft() << "-" << i.bottomRight();
		}
	}

	return ok;
}

void dump_events(rectangle_partitionner_private & p)
{
	QJsonObject doc;

	QJsonArray initial_size;
	initial_size.push_back(p.initial_size.width());
	initial_size.push_back(p.initial_size.height());
	doc["initial_size"] = initial_size;

	QJsonArray rectangles_float;
	for (QRectF & i: p.rectangles_float)
	{
		QJsonObject obj;
		obj["x"] = i.x();
		obj["y"] = i.y();
		obj["w"] = i.width();
		obj["h"] = i.height();
		rectangles_float.push_back(obj);
	}
	doc["rectangles_float"] = rectangles_float;

	QJsonArray rectangles_int;
	for (QRect & i: p.rectangles_int)
	{
		QJsonObject obj;
		obj["x"] = i.x();
		obj["y"] = i.y();
		obj["w"] = i.width();
		obj["h"] = i.height();
		rectangles_float.push_back(obj);
	}
	doc["rectangles_int"] = rectangles_int;

	QJsonArray events;
	for (auto & [i, j]: p.events)
	{
		QJsonObject obj;
		switch (i)
		{
			case rectangle_partitionner_private::mouse_event_type::move:
				obj["type"] = "move";
				obj["x"] = j->position().x();
				obj["y"] = j->position().y();
				obj["buttons"] = (int)j->buttons();
				rectangles_float.push_back(obj);
				break;
			case rectangle_partitionner_private::mouse_event_type::press:
				obj["type"] = "press";
				obj["x"] = j->position().x();
				obj["y"] = j->position().y();
				obj["buttons"] = (int)j->buttons();
				rectangles_float.push_back(obj);
				break;
			case rectangle_partitionner_private::mouse_event_type::release:
				obj["type"] = "release";
				obj["x"] = j->position().x();
				obj["y"] = j->position().y();
				obj["buttons"] = (int)j->buttons();
				rectangles_float.push_back(obj);
				break;
		}
	}
	doc["events"] = events;

	std::ofstream f("events.json");
	f << QString::fromUtf8(QJsonDocument(doc).toJson()).toStdString();
}

} // namespace

rectangle_partitionner::rectangle_partitionner(QWidget * parent) :
        QFrame(parent)
{
	// setBackgroundRole(QPalette::Window);
	// setAutoFillBackground(true);

	p = new rectangle_partitionner_private;

	setMouseTracking(true);
	// setFocusPolicy(Qt::FocusPolicy::ClickFocus);

	std::vector<QRectF> encoders;

	encoders.push_back(QRectF{0, 0, 1, 1});
	set_rectangles(encoders);

	m_paint = [](QPainter & painter, QRect rect, const QVariant & data, int index, bool selected) {
		if (selected)
		{
			painter.fillRect(rect.adjusted(1, 1, 0, 0), QColorConstants::Cyan);
		}

		QString text = QString("%1").arg(index + 1);
		painter.drawText(rect, Qt::AlignCenter, text);
	};
}

rectangle_partitionner::~rectangle_partitionner()
{
	delete p;
}

void rectangle_partitionner::paintEvent(QPaintEvent * event)
{
	QFrame::paintEvent(event);

	QPainter painter(this);

	if (m_paint)
	{
		int n = 0;
		for (QRect & i: rectangles_position)
		{
			painter.drawRect(i);
			bool is_selected = n == m_selected_index and isEnabled();
			m_paint(painter, i, m_rectangles_data[n], n, is_selected);

			n++;
		}
	}

	if (not p->hovered.empty() and p->selection.empty())
	{
		painter.setPen(QPen(Qt::blue, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

		QList<QLine> lines;
		for (const edge & i: p->hovered)
		{
			switch (i.edge_side)
			{
				case side::top:
				case side::bottom:
					lines.emplace_back(QLine(i.min, i.position, i.max, i.position));
					break;

				case side::left:
				case side::right:
					lines.emplace_back(QLine(i.position, i.min, i.position, i.max));
					break;
			}
		}
		painter.drawLines(lines);

		auto r = frameRect();
		r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());
		auto horz_partitionned = partition_edges(horizontal_edges(r, rectangles_position));
		auto vert_partitionned = partition_edges(vertical_edges(r, rectangles_position));
	}
	else if (cursor() == Qt::SplitHCursor or cursor() == Qt::SplitVCursor and not p->split_line.isNull())
	{
		auto now = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now().time_since_epoch()).count();

		QPen pen(Qt::black, 2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
		pen.setDashPattern({5, 5});
		pen.setDashOffset(-now * 15);
		painter.setPen(pen);

		painter.drawLine(p->split_line);
		update();
	}
}

void rectangle_partitionner::resizeEvent(QResizeEvent * event)
{
	update_rectangles_position();

	p->rectangles_float = m_rectangles;
	p->rectangles_int = rectangles_position;
	p->initial_size = event->size();
	for (auto [i, j]: p->events)
		delete j;
	p->events.clear();
}

void rectangle_partitionner::mouseMoveEvent(QMouseEvent * event)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(m_rectangles.size() == rectangles_position.size());
	if (event->buttons() != Qt::NoButton)
		p->events.emplace_back(rectangle_partitionner_private::mouse_event_type::move, event->clone());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	if (not p->selection.empty())
	{
		int position;

		if (p->selection.front().edge_side == side::top or p->selection.front().edge_side == side::bottom)
			position = event->position().y();
		else
			position = event->position().x();

		position = std::clamp(position, p->min_drag_position, p->max_drag_position);

		for (edge & e: p->selection)
		{
			switch (e.edge_side)
			{
				case side::top:
					m_rectangles[e.rectangle_index].setTop(double(position - r.y()) / (r.height() - 1));
					rectangles_position[e.rectangle_index].setTop(position);
					break;

				case side::bottom:
					m_rectangles[e.rectangle_index].setBottom(double(position - r.y()) / (r.height() - 1));
					rectangles_position[e.rectangle_index].setBottom(position);
					break;

				case side::left:
					m_rectangles[e.rectangle_index].setLeft(double(position - r.x()) / (r.width() - 1));
					rectangles_position[e.rectangle_index].setLeft(position);
					break;

				case side::right:
					m_rectangles[e.rectangle_index].setRight(double(position - r.x()) / (r.width() - 1));
					rectangles_position[e.rectangle_index].setRight(position);
					break;
			}
		}

		if (!assert_rectangle_list_is_partition(r, rectangles_position))
		{
			dump_events(*p);
			abort();
		}

		update();
		rectangles_change(m_rectangles);
	}
	else
	{
		p->hovered.clear();

		if (auto horz_edges = hovered_edges(event->position().toPoint(), partition_edges(horizontal_edges(r, rectangles_position)), true); not horz_edges.empty())
		{
			p->hovered = std::move(horz_edges);
			update();
		}
		else if (auto vert_edges = hovered_edges(event->position(), partition_edges(vertical_edges(r, rectangles_position)), false); not vert_edges.empty())
		{
			p->hovered = std::move(vert_edges);
			update();
		}
		else
		{
			int n = hovered_rectangle(event->position().toPoint(), rectangles_position);
		}
	}

	auto new_cursor = get_cursor_shape(event->position(), r, rectangles_position, 5, 15);
	if (new_cursor != cursor())
	{
		update();
		setCursor(new_cursor);
	}

	if (cursor() == Qt::SplitHCursor)
	{
		int n = hovered_rectangle(event->pos(), rectangles_position);
		p->split_line = {event->pos().x(), rectangles_position[n].top(), event->pos().x(), rectangles_position[n].bottom()};
		update();
	}
	else if (cursor() == Qt::SplitVCursor)
	{
		int n = hovered_rectangle(event->pos(), rectangles_position);
		p->split_line = {rectangles_position[n].left(), event->pos().y(), rectangles_position[n].right(), event->pos().y()};
		update();
	}
	else
	{
		p->split_line = {0, 0, 0, 0};
	}
}

void rectangle_partitionner::mousePressEvent(QMouseEvent * event)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(m_rectangles.size() == rectangles_position.size());

	p->events.emplace_back(rectangle_partitionner_private::mouse_event_type::press, event->clone());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	p->selection.clear();
	p->split_line = {0, 0, 0, 0};

	if (auto horz_edges = hovered_edges(event->position(), partition_edges(horizontal_edges(r, rectangles_position)), true); not horz_edges.empty())
	{
		p->selection = std::move(horz_edges);
	}
	else if (auto vert_edges = hovered_edges(event->position(), partition_edges(vertical_edges(r, rectangles_position)), false); not vert_edges.empty())
	{
		p->selection = std::move(vert_edges);
	}
	else if (int n = hovered_rectangle(event->position().toPoint(), rectangles_position); n >= 0)
	{
		QRect & hovered_position = rectangles_position[n];
		QRectF & hovered = m_rectangles[n];

		// Compute distance to edges
		double dx = std::min(event->position().x() - hovered_position.left(), hovered_position.right() - event->position().x());
		double dy = std::min(event->position().y() - hovered_position.top(), hovered_position.bottom() - event->position().y());

		if (dx < 15)
		{
			// Split vertically
			QRect new_rectangle_position = hovered_position;
			QRectF new_rectangle = hovered;

			hovered_position.setBottom(event->position().y());
			new_rectangle_position.setTop(hovered_position.bottom());

			hovered.setBottom((event->position().y() - r.y()) / (r.height() - 1));
			new_rectangle.setTop(hovered.bottom());

			m_rectangles.push_back(new_rectangle);
			rectangles_position.push_back(new_rectangle_position);
			m_rectangles_data.push_back(m_rectangles_data[n]);
			update();
			rectangles_change(m_rectangles);

			if (!assert_rectangle_list_is_partition(r, rectangles_position))
			{
				dump_events(*p);
				abort();
			}
		}
		else if (dy < 15)
		{
			// Split horizontally
			QRect new_rectangle_position = hovered_position;
			QRectF new_rectangle = hovered;

			hovered_position.setRight(event->position().x());
			new_rectangle_position.setLeft(hovered_position.right());

			hovered.setRight((event->position().x() - r.x()) / (r.width() - 1));
			new_rectangle.setLeft(hovered.right());

			m_rectangles.push_back(new_rectangle);
			rectangles_position.push_back(new_rectangle_position);
			m_rectangles_data.push_back(m_rectangles_data[n]);
			update();
			rectangles_change(m_rectangles);

			if (!assert_rectangle_list_is_partition(r, rectangles_position))
			{
				dump_events(*p);
				abort();
			}
		}
		else
		{
			set_selected_index(n);
		}
	}

	if (not p->selection.empty())
	{
		p->hovered.clear();

		p->min_drag_position = std::numeric_limits<int>::lowest();
		p->max_drag_position = std::numeric_limits<int>::max();

		for (auto & e: p->selection)
		{
			switch (e.edge_side)
			{
				case side::top:
					p->max_drag_position = std::min(p->max_drag_position, rectangles_position[e.rectangle_index].bottom());
					break;
				case side::bottom:
					p->min_drag_position = std::max(p->min_drag_position, rectangles_position[e.rectangle_index].top());
					break;
				case side::left:
					p->max_drag_position = std::min(p->max_drag_position, rectangles_position[e.rectangle_index].right());
					break;
				case side::right:
					p->min_drag_position = std::max(p->min_drag_position, rectangles_position[e.rectangle_index].left());
					break;
			}
		}
	}
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(m_rectangles.size() == rectangles_position.size());
}

void rectangle_partitionner::mouseReleaseEvent(QMouseEvent * event)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(m_rectangles.size() == rectangles_position.size());
	p->events.emplace_back(rectangle_partitionner_private::mouse_event_type::release, event->clone());

	p->selection.clear();

	// Delete empty rectangles
	auto i1 = m_rectangles.begin();
	auto i2 = rectangles_position.begin();
	auto i3 = m_rectangles_data.begin();

	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(m_rectangles.size() == rectangles_position.size());

	bool changed = false;
	for (; i1 != m_rectangles.end();)
	{
		if (i2->width() <= 1 or i2->height() <= 1)
		{
			i1 = m_rectangles.erase(i1);
			i2 = rectangles_position.erase(i2);
			i3 = m_rectangles_data.erase(i3);
			changed = true;
		}
		else
		{
			++i1;
			++i2;
			++i3;
		}
	}
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(m_rectangles.size() == rectangles_position.size());

	if (changed)
	{
		rectangles_change(m_rectangles);
		update();
	}
}

void rectangle_partitionner::leaveEvent(QEvent * event)
{
	p->split_line = {0, 0, 0, 0};
}

void rectangle_partitionner::keyPressEvent(QKeyEvent * event)
{
	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());
	qDebug() << "Bounding box:" << r;
	int n = 1;
	for (auto & i: rectangles_position)
	{
		qDebug() << "Rectangle" << (n++) << ":" << i.topLeft() << "-" << i.bottomRight();
	}
}

void rectangle_partitionner::set_rectangles(const rectangle_list & value)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	auto old_value = m_rectangles;

	m_rectangles = value;
	update_rectangles_position();

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());
	if (not assert_rectangle_list_is_partition(r, rectangles_position))
	{
		m_rectangles = old_value;
		update_rectangles_position();

		throw std::runtime_error("Invalid partitionning");
	}

	m_rectangles_data.resize(m_rectangles.size());

	if (m_selected_index >= m_rectangles.size())
		set_selected_index(m_rectangles.size() - 1);

	update();
	rectangles_change(m_rectangles);
	assert(m_rectangles.size() == m_rectangles_data.size());
}

void rectangle_partitionner::set_selected_index(int new_index)
{
	new_index = std::clamp<int>(new_index, 0, m_rectangles.size());

	if (new_index != m_selected_index)
	{
		m_selected_index = new_index;
		update();

		selected_index_change(new_index);
	}
}

void rectangle_partitionner::set_rectangles_data(const data_list & value)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	m_rectangles_data = value;
	m_rectangles_data.resize(m_rectangles.size());
	assert(m_rectangles.size() == m_rectangles_data.size());

	update();
}

void rectangle_partitionner::set_rectangles_data(int index, QVariant value)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(index < m_rectangles_data.size());
	m_rectangles_data[index] = value;
	assert(m_rectangles.size() == m_rectangles_data.size());

	update();
}

void rectangle_partitionner::set_paint(paint_function paint)
{
	m_paint = paint;
	update();
}

void rectangle_partitionner::update_rectangles_position()
{
	rectangles_position.clear();
	rectangles_position.reserve(m_rectangles.size());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	for (QRectF & i: m_rectangles)
	{
		int x1 = std::round(r.x() + i.x() * (r.width() - 1));
		int x2 = std::round(r.x() + (i.x() + i.width()) * (r.width() - 1));

		int y1 = std::round(r.y() + i.y() * (r.height() - 1));
		int y2 = std::round(r.y() + (i.y() + i.height()) * (r.height() - 1));
		rectangles_position.emplace_back(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
	}
	assert(m_rectangles.size() == rectangles_position.size());
}

#include "moc_rectangle_partitionner.cpp"
