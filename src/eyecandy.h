/* ghostess - A GUI host for DSSI plugins.
 *
 * Much of this code comes from aube 0.30.2, copyright (c) 2002
 * Conrad Parker, with modifications by Sean Bolton,
 * copyright (c) 2006, 2012.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef __EYECANDY_H__
#define __EYECANDY_H__

#include <gdk/gdk.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ==== Blinky ==== */

#define TYPE_BLINKY             (blinky_get_type ())
#define BLINKY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_BLINKY, Blinky))
#define BLINKY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_BLINKY, BlinkyClass))
#define IS_BLINKY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_BLINKY))

  typedef struct _Blinky Blinky;
  typedef struct _BlinkyClass BlinkyClass;

  struct _Blinky {
    GtkWidget widget;

    guint state;
  };

  struct _BlinkyClass {
    GtkWidgetClass parent_class;
  };

  GType      blinky_get_type (void) G_GNUC_CONST;
  GtkWidget *blinky_new(guint state);
  guint blinky_get_state(Blinky * blinky);
  void blinky_set_state(Blinky * blinky, guint state);

G_END_DECLS

#endif  /* __EYECANDY_H__ */
