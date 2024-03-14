#pragma once

#include <QtWidgets/QtWidgets>

#include <ida.hpp>

class PaintForm : public QWidget {
  Q_OBJECT

  uint16_t pal[16];
  QScrollBar* scroll;

  int getFullTiles();
public:
  PaintForm();
  void setScrollBar(QScrollBar* sb);
public slots:
  void textChanged(const QString&);
  void scrollChanged(int value);
protected:
  void paintEvent(QPaintEvent* event);
};

