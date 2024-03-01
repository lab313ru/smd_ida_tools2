//#include <raylib.h>
//
//#define RAYGUI_IMPLEMENTATION
//#define RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT 0
//#define RAYGUI_NO_ICONS
//#include <raygui.h>
//
//#undef RAYGUI_IMPLEMENTATION
//#include "ray/style_dark.h"
//
//static const int window_w = 320;
//static const int window_h = 240;

#include <dbg.hpp>

#include "paintform.h"

#define VDP_TILE_W 8
#define VDP_TILE_H 8
#define VDP_TILE_ZOOM 4

#define VDP_TILES_IN_ROW (this->width() / (VDP_TILE_W * VDP_TILE_ZOOM))
#define VDP_TILES_IN_COL (this->height() / (VDP_TILE_H * VDP_TILE_ZOOM))

static const uint16_t pal_raw[16] = {
  0x0CCC, 0x0444, 0x0886, 0x0CAA, 0x0026, 0x022C, 0x026C, 0x02CC,
  0x0220, 0x0240, 0x0C86, 0x0420, 0x0A42, 0x0462, 0x08AC, 0x0000,
};

PaintForm::PaintForm() {
  memcpy(pal, pal_raw, sizeof(pal_raw));
}

void PaintForm::textChanged(const QString& text) {
  if (text.isEmpty()) {
    memcpy(pal, pal_raw, sizeof(pal_raw));
  }
  else {
    bool ok;
    ea_t addr = text.toUInt(&ok, 16);

    if (!ok) {
      memcpy(pal, pal_raw, sizeof(pal));
    }
    else {
      ssize_t ss = read_dbg_memory(addr, pal, sizeof(pal));
      if (ss == -1 || ss == 0) {
        get_bytes(pal, sizeof(pal), addr, GMB_READALL);
      }
    }
  }

  update();
}

void PaintForm::paintEvent(QPaintEvent* event) {
  ea_t start = get_screen_ea();

  if (start == BADADDR || start == prev_ea)
    return;

  ea_t end = start + VDP_TILES_IN_ROW * VDP_TILES_IN_COL;
  ea_t tiles = end - start;

  QPainter painter(this);
  painter.begin(this);

  QPen myPen(Qt::red, VDP_TILE_ZOOM, Qt::SolidLine);
  painter.setPen(myPen);

  QColor palette[16];

  for (int i = 0; i < _countof(palette); ++i)
  {
    uint8 r = (uint8)(((pal[i] >> 0) & 0xE) << 4);
    uint8 g = (uint8)(((pal[i] >> 4) & 0xE) << 4);
    uint8 b = (uint8)(((pal[i] >> 8) & 0xE) << 4);
    palette[i] = QColor(r, g, b);
  }

  for (ea_t i = 0; i < tiles; ++i)
  {
    for (int y = 0; y < VDP_TILE_H; ++y)
    {
      for (int x = 0; x < (VDP_TILE_W / 2); ++x)
      {
        int _x = (i % VDP_TILES_IN_ROW) * VDP_TILE_W + x * 2;
        int _y = (i / VDP_TILES_IN_ROW) * VDP_TILE_H + y;

        ea_t addr = start + i * 0x20 + y * (VDP_TILE_W / 2) + x;
        uint32 t = 0;
        if (!get_dbg_byte(&t, addr))
        {
          t = get_db_byte(start + i * 0x20 + y * (VDP_TILE_W / 2) + x);
        }
        else
        {
          t = t & 0xFF;
        }

        myPen.setColor(palette[t >> 4]);
        painter.setPen(myPen);
        painter.drawPoint((_x + 0) * VDP_TILE_ZOOM, _y * VDP_TILE_ZOOM);

        myPen.setColor(palette[t & 0x0F]);
        painter.setPen(myPen);
        painter.drawPoint((_x + 1) * VDP_TILE_ZOOM, _y * VDP_TILE_ZOOM);
      }
    }
  }

  painter.end();
}

