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

#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>

#include "eyecandy.h"

#define BLINKY_DEFAULT_WIDTH   4
#define BLINKY_DEFAULT_HEIGHT 16

/* ==== Blinky ==== */

/*  Forward declarations  */

static void blinky_class_init(BlinkyClass * class);
static void blinky_init(Blinky * blinky);
static void blinky_destroy(GtkObject * object);
static void blinky_realize(GtkWidget * widget);
static gint blinky_expose(GtkWidget * widget, GdkEventExpose * event);
static void blinky_size_request(GtkWidget * widget, GtkRequisition * requisition);
static void blinky_size_allocate(GtkWidget * widget, GtkAllocation * allocation);
static void blinky_update(Blinky * blinky);

/*  Local data  */

static GdkColor col_darkgreen, col_green;
static GtkStyle *blinky_style = NULL;

G_DEFINE_TYPE(Blinky, blinky, GTK_TYPE_WIDGET);

static void
blinky_class_init(BlinkyClass * class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;

  blinky_parent_class = gtk_type_class(gtk_widget_get_type());

  object_class->destroy = blinky_destroy;

  widget_class->realize = blinky_realize;
  widget_class->expose_event = blinky_expose;
  widget_class->size_request = blinky_size_request;
  widget_class->size_allocate = blinky_size_allocate;

  col_green.red = 0;
  col_green.green = 0xFFFF;
  col_green.blue = 0;
  gdk_color_alloc(gdk_colormap_get_system(), &col_green);

  col_darkgreen.red = 0;
  col_darkgreen.green = 0x5555;
  col_darkgreen.blue = 0;
  gdk_color_alloc(gdk_colormap_get_system(), &col_darkgreen);

}

static void
blinky_init(Blinky * blinky)
{
  blinky->state = 0;
}

GtkWidget *
blinky_new(guint state)
{
  Blinky *blinky = g_object_new (TYPE_BLINKY, NULL);

  blinky_set_state(blinky, state);

  return GTK_WIDGET(blinky);
}

static void
blinky_destroy(GtkObject * object)
{
  Blinky *blinky;

  g_return_if_fail(object != NULL);
  g_return_if_fail(IS_BLINKY(object));

  blinky = BLINKY(object);

  /* unref contained widgets */

  if (GTK_OBJECT_CLASS(blinky_parent_class)->destroy)
    (*GTK_OBJECT_CLASS(blinky_parent_class)->destroy) (object);
}

guint
blinky_get_state(Blinky * blinky)
{
  g_return_val_if_fail(blinky != NULL, 0);
  g_return_val_if_fail(IS_BLINKY(blinky), 0);

  return blinky->state;
}

void
blinky_set_state(Blinky * blinky, guint state)
{
  g_return_if_fail(blinky != NULL);
  g_return_if_fail(IS_BLINKY(blinky));

  blinky->state = state;

  blinky_update(blinky);
}

static void
blinky_realize(GtkWidget * widget)
{
  Blinky *blinky;
  GdkWindowAttr attributes;
  gint attributes_mask;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(IS_BLINKY(widget));

  GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
  blinky = BLINKY(widget);

  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.event_mask = gtk_widget_get_events(widget) |
                              GDK_EXPOSURE_MASK;
  attributes.visual = gtk_widget_get_visual(widget);
  attributes.colormap = gtk_widget_get_colormap(widget);

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  widget->window = gdk_window_new(widget->parent->window, &attributes,
				  attributes_mask);

  widget->style = gtk_style_attach(widget->style, widget->window);

  gdk_window_set_user_data(widget->window, widget);

  gtk_style_set_background(widget->style, widget->window, GTK_STATE_ACTIVE);
}

static void
blinky_size_request(GtkWidget * widget, GtkRequisition * requisition)
{
  requisition->width = BLINKY_DEFAULT_WIDTH;
  requisition->height = BLINKY_DEFAULT_HEIGHT;
}

static void
blinky_size_allocate(GtkWidget * widget, GtkAllocation * allocation)
{
  Blinky *blinky;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(IS_BLINKY(widget));
  g_return_if_fail(allocation != NULL);

  widget->allocation = *allocation;
  if (GTK_WIDGET_REALIZED(widget)) {
    blinky = BLINKY(widget);

    gdk_window_move_resize(widget->window,
       allocation->x, allocation->y, allocation->width, allocation->height);
  }
}

static gint
blinky_expose(GtkWidget * widget, GdkEventExpose * event)
{
  gdk_window_clear_area(widget->window, 0, 0,
			widget->allocation.width, widget->allocation.height); // !FIX! clear to black?

  if (!blinky_style) {
    blinky_style = gtk_style_new();
    blinky_style->fg_gc[GTK_STATE_NORMAL] =
      gdk_gc_new(widget->window);
  }
  if (BLINKY(widget)->state)
      gdk_gc_set_foreground(blinky_style->fg_gc[GTK_STATE_NORMAL], &col_green);
  else
      gdk_gc_set_foreground(blinky_style->fg_gc[GTK_STATE_NORMAL], &col_darkgreen);

  gdk_draw_rectangle(widget->window, blinky_style->fg_gc[widget->state],
                     TRUE, 0, 0,
                     widget->allocation.width, widget->allocation.height
                     );

  return FALSE;
}


static void
blinky_update(Blinky * blinky)
{
  gtk_widget_draw(GTK_WIDGET(blinky), NULL);
}

