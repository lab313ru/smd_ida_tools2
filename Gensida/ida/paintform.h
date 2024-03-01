#pragma once

#include <QtWidgets/QtWidgets>

#include <ida.hpp>

class PaintForm : public QWidget {
  Q_OBJECT

  ea_t prev_ea = BADADDR;
  uint16_t pal[16];
public:
  PaintForm();
public slots:
  void textChanged(const QString&);
protected:
  void paintEvent(QPaintEvent* event);
};

