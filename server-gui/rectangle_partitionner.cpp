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
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionFocusRect>

#include <cmath>
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

	double position = 0;
	double min = std::numeric_limits<double>::max();
	double max = std::numeric_limits<double>::lowest();
};

int hovered_rectangle(QPointF position, QList<QRectF> rectangles)
{
	int n = 0;
	for (QRectF & i: rectangles)
	{
		if (i.contains(position))
			return n;
		n++;
	}

	return -1;
}

Qt::CursorShape get_cursor_shape(QPointF position, QRectF bounding_box, QList<QRectF> rectangles, double margin_move_edges, double margin_split_edges)
{
	bounding_box.adjust(margin_move_edges, margin_move_edges, -margin_move_edges, -margin_move_edges);

	for (QRectF & i: rectangles)
	{
		if (i.contains(position))
		{
			QPointF rel_pos = position - i.topLeft();
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

std::vector<edge> horizontal_edges(QRectF bounding_box, QList<QRectF> rectangles)
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

std::vector<edge> vertical_edges(QRectF bounding_box, QList<QRectF> rectangles)
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

std::vector<std::vector<edge>> partition_edges(std::vector<edge> edges, double margin = 0.001)
{
	auto comp = [margin](const edge & a, const edge & b) {
		if (a.position < b.position - margin)
			return true;
		if (a.position > b.position + margin)
			return false;

		if (a.min < b.min - margin)
			return true;
		if (a.min > b.min + margin)
			return false;

		if (a.max < b.max - margin)
			return true;
		return false;
	};

	std::ranges::sort(edges, comp);

	std::vector<std::vector<edge>> partitionned;

	double current_position = nan("");
	double current_max = 0;

	for (auto & e: edges)
	{
		if (std::abs(e.position - current_position) > margin)
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
		std::swap(position.rx(), position.ry());

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

} // namespace

struct rectangle_partitionner_private
{
	std::vector<edge> selection;

	double min_drag_position;
	double max_drag_position;
};

rectangle_partitionner::rectangle_partitionner(QWidget * parent) :
        QFrame(parent)
{
	// setBackgroundRole(QPalette::Window);
	// setAutoFillBackground(true);

	p = new rectangle_partitionner_private;

	setMouseTracking(true);

	QList<QRectF> encoders;

	encoders.push_back(QRectF{0, 0, 1, 1});
	set_rectangles(encoders);

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	auto horz_partitionned = partition_edges(horizontal_edges(r, rectangles_position));
	auto vert_partitionned = partition_edges(vertical_edges(r, rectangles_position));
}

rectangle_partitionner::~rectangle_partitionner()
{
	delete p;
}

void rectangle_partitionner::paintEvent(QPaintEvent * event)
{
	QFrame::paintEvent(event);

	QPainter painter(this);

	int n = 0;
	for (QRectF & i: rectangles_position)
	{
		painter.drawRect(i);
		if (n == m_selected_index)
		{
			painter.fillRect(i.adjusted(1, 1, 0, 0), QColorConstants::Cyan);
		}

		// TODO custom text
		QString text = QString("%1").arg(++n);
		painter.drawText(i, Qt::AlignCenter, text);
	}
}

void rectangle_partitionner::resizeEvent(QResizeEvent * event)
{
	update_rectangles_position();
}

void rectangle_partitionner::mouseMoveEvent(QMouseEvent * event)
{
	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	if (not p->selection.empty())
	{
		double position;

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
					m_rectangles[e.rectangle_index].setTop((position - r.y()) / r.height());
					rectangles_position[e.rectangle_index].setTop(position);
					break;

				case side::bottom:
					m_rectangles[e.rectangle_index].setBottom((position - r.y()) / r.height());
					rectangles_position[e.rectangle_index].setBottom(position);
					break;

				case side::left:
					m_rectangles[e.rectangle_index].setLeft((position - r.x()) / r.width());
					rectangles_position[e.rectangle_index].setLeft(position);
					break;

				case side::right:
					m_rectangles[e.rectangle_index].setRight((position - r.x()) / r.width());
					rectangles_position[e.rectangle_index].setRight(position);
					break;
			}
		}

		update();
		rectangles_change(m_rectangles);
	}

	setCursor(get_cursor_shape(event->position(), r, rectangles_position, 5, 15));
}

void rectangle_partitionner::mousePressEvent(QMouseEvent * event)
{
	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	p->selection.clear();

	if (auto horz_edges = hovered_edges(event->position(), partition_edges(horizontal_edges(r, rectangles_position)), true); not horz_edges.empty())
	{
		p->selection = std::move(horz_edges);
	}
	else if (auto vert_edges = hovered_edges(event->position(), partition_edges(vertical_edges(r, rectangles_position)), false); not vert_edges.empty())
	{
		p->selection = std::move(vert_edges);
	}
	else if (int n = hovered_rectangle(event->position(), rectangles_position); n >= 0)
	{
		QRectF & hovered_position = rectangles_position[n];
		QRectF & hovered = m_rectangles[n];

		// Compute distance to edges
		double dx = std::min(event->position().x() - hovered_position.left(), hovered_position.right() - event->position().x());
		double dy = std::min(event->position().y() - hovered_position.top(), hovered_position.bottom() - event->position().y());

		if (dx < 15)
		{
			// Split vertically
			QRectF new_rectangle_position = hovered_position;
			QRectF new_rectangle = hovered;

			hovered_position.setBottom(event->position().y());
			new_rectangle_position.setTop(hovered_position.bottom());

			hovered.setBottom((event->position().y() - r.y()) / r.height());
			new_rectangle.setTop(hovered.bottom());

			m_rectangles.push_back(new_rectangle);
			rectangles_position.push_back(new_rectangle_position);
			m_rectangles_data.push_back(m_rectangles_data[n]);
			update();
			rectangles_change(m_rectangles);
		}
		else if (dy < 15)
		{
			// Split horizontally
			QRectF new_rectangle_position = hovered_position;
			QRectF new_rectangle = hovered;

			hovered_position.setRight(event->position().x());
			new_rectangle_position.setLeft(hovered_position.right());

			hovered.setRight((event->position().x() - r.x()) / r.width());
			new_rectangle.setLeft(hovered.right());

			m_rectangles.push_back(new_rectangle);
			rectangles_position.push_back(new_rectangle_position);
			m_rectangles_data.push_back(m_rectangles_data[n]);
			update();
			rectangles_change(m_rectangles);
		}
		else
		{
			set_selected_index(n);
		}
	}

	if (not p->selection.empty())
	{
		p->min_drag_position = std::numeric_limits<double>::lowest();
		p->max_drag_position = std::numeric_limits<double>::max();

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
}

void rectangle_partitionner::mouseReleaseEvent(QMouseEvent * event)
{
	p->selection.clear();

	// Delete empty rectangles
	auto rectangles = m_rectangles;

	auto i1 = rectangles.begin();
	auto i2 = m_rectangles_data.begin();

	assert(m_rectangles.size() == m_rectangles_data.size());

	bool changed = false;
	for (; i1 != rectangles.end();)
	{
		if (i1->width() < 0.001 or i1->height() < 0.001)
		{
			i1 = rectangles.erase(i1);
			i2 = m_rectangles_data.erase(i2);
			changed = true;
		}
		else
		{
			++i1;
			++i2;
		}
	}

	if (changed)
		set_rectangles(rectangles);
}

void rectangle_partitionner::set_rectangles(const rectangle_list & value)
{
	m_rectangles = value;
	m_rectangles_data.resize(m_rectangles.size());
	update_rectangles_position();

	if (m_selected_index >= m_rectangles.size())
		set_selected_index(m_rectangles.size() - 1);

	update();
	rectangles_change(m_rectangles);
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
	m_rectangles_data = value;
	m_rectangles_data.resize(m_rectangles.size());
}

void rectangle_partitionner::set_rectangles_data(int index, QVariant value)
{
	assert(index < m_rectangles_data.size());
	m_rectangles_data[index] = value;
}

void rectangle_partitionner::update_rectangles_position()
{
	rectangles_position.clear();
	rectangles_position.reserve(m_rectangles.size());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	for (QRectF & i: m_rectangles)
	{
		int x1 = r.x() + i.x() * r.width();
		int x2 = r.x() + (i.x() + i.width()) * r.width();

		int y1 = r.y() + i.y() * r.height();
		int y2 = r.y() + (i.y() + i.height()) * r.height();
		rectangles_position.emplace_back(x1, y1, x2 - x1, y2 - y1);
	}
}

#include "moc_rectangle_partitionner.cpp"
