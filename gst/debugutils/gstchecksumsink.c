/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstchecksumsink.h"
#include <dirent.h>
#include <string.h>

/* properties */
enum
{
  PROP_0,
  PROP_HASH,
  PROP_CHECKSUM_FILE,
  PROP_STATUS
};

static void gst_checksum_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_checksum_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_checksum_sink_dispose (GObject * object);
static void gst_checksum_sink_finalize (GObject * object);

static gboolean gst_checksum_sink_start (GstBaseSink * sink);
static gboolean gst_checksum_sink_stop (GstBaseSink * sink);
static GstFlowReturn
gst_checksum_sink_render (GstBaseSink * sink, GstBuffer * buffer);

static void gst_checksum_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_checksum_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* Template definition */

/*enum
{
  PROP_0,
  PROP_HASH,
};*/

static GstStaticPadTemplate gst_checksum_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* class initialization */

#define GST_TYPE_CHECKSUM_SINK_HASH (gst_checksum_sink_hash_get_type ())
static GType
gst_checksum_sink_hash_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {G_CHECKSUM_MD5, "MD5", "md5"},
      {G_CHECKSUM_SHA1, "SHA-1", "sha1"},
      {G_CHECKSUM_SHA256, "SHA-256", "sha256"},
      {G_CHECKSUM_SHA512, "SHA-512", "sha512"},
      {0, NULL, NULL},
    };

    gtype = g_enum_register_static ("GstChecksumSinkHash", values);
  }
  return gtype;
}

#define gst_checksum_sink_parent_class parent_class
G_DEFINE_TYPE (GstChecksumSink, gst_checksum_sink, GST_TYPE_BASE_SINK);

static void
gst_checksum_sink_class_init (GstChecksumSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_checksum_sink_set_property;
  gobject_class->get_property = gst_checksum_sink_get_property;
  g_object_class_install_property (gobject_class, PROP_CHECKSUM_FILE,
      g_param_spec_string ("checksum-file", "checksum-file", "checksum file name",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_STATUS,
      g_param_spec_int ("status", "status", "status of checksum comparision",
          -1, 9999999, 0, G_PARAM_READWRITE));
  gobject_class->dispose = gst_checksum_sink_dispose;
  gobject_class->finalize = gst_checksum_sink_finalize;
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_checksum_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_checksum_sink_stop);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_checksum_sink_render);

  gst_element_class_add_static_pad_template (element_class,
      &gst_checksum_sink_sink_template);

  g_object_class_install_property (gobject_class, PROP_HASH,
      g_param_spec_enum ("hash", "Hash", "Checksum type",
          gst_checksum_sink_hash_get_type (), G_CHECKSUM_SHA1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class, "Checksum sink",
      "Debug/Sink", "Calculates a checksum for buffers",
      "David Schleef <ds@schleef.org>");
}

static void
gst_checksum_sink_init (GstChecksumSink * checksumsink)
{
  gst_base_sink_set_sync (GST_BASE_SINK (checksumsink), FALSE);
  checksumsink->hash = G_CHECKSUM_SHA1;
  checksumsink->filename = malloc(sizeof(gchar) * 200);
  checksumsink->status = 0;
}

static void
gst_checksum_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstChecksumSink *checksumsink = GST_CHECKSUM_SINK (object);

  switch (prop_id) {
    case PROP_HASH:
      checksumsink->hash = g_value_get_enum (value);
      break;
    case PROP_CHECKSUM_FILE:
      strcpy(checksumsink->filename, g_value_get_string(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_checksum_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstChecksumSink *filter = GST_CHECKSUM_SINK (object);

  switch (prop_id) {
    case PROP_HASH:
      g_value_set_enum (value, filter->hash);
      break;
    case PROP_CHECKSUM_FILE:
      g_value_set_string (value, filter->filename);
      break;
    case PROP_STATUS:
      g_value_set_int (value, filter->status);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_checksum_sink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_checksum_sink_finalize (GObject * object)
{
  GstChecksumSink *sink1 = GST_CHECKSUM_SINK(object);
  if(sink1->fd != NULL)
    fclose(sink1->fd);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_checksum_sink_start (GstBaseSink * sink)
{
  struct stat stat1;

  GstChecksumSink *sink1 = GST_CHECKSUM_SINK(sink);
  memset((void *) &stat1, 0, sizeof(stat1));

  if(sink1->fd != NULL)
 {
    fclose(sink1->fd);
   sink1->fd = NULL;
  }

  if (stat(sink1->filename, &stat1) == -1 && errno == ENOENT) {
    g_print("Missing checksum file: %s\nGenerating new checksum file... \n", sink1->filename);
    sink1->fd = fopen(sink1->filename, "w");
   if(sink1->fd == NULL)
    {
      g_print("can't open %s for writing! \n", sink1->filename);
    }
    sink1->generate_hash = 1;
  } else {
    sink1->fd = fopen(sink1->filename, "r");
    sink1->generate_hash = 0;
  }
  sink1->status = 0;
  return TRUE;
}

static gboolean
gst_checksum_sink_stop (GstBaseSink * sink)
{
  return TRUE;
}

static GstFlowReturn
gst_checksum_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  gchar *checksum = NULL, *r_hash = NULL;
  GstMapInfo map;
  static int count = 0;
  GstChecksumSink *sink1 = GST_CHECKSUM_SINK(sink);
  GstChecksumSink *checksumsink;

  checksumsink = GST_CHECKSUM_SINK (sink);
  gst_buffer_map (buffer, &map, GST_MAP_READ);
  checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1, map.data, map.size);
  gst_buffer_unmap (buffer, &map);
  g_print ("%" GST_TIME_FORMAT " %s\n",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), checksum);
  g_print ("%d == > %s\n", count++, checksum);
  checksum = g_strconcat(checksum, "\n", NULL);

 if(sink1->generate_hash == 1) {
    fwrite(checksum, sizeof(char), strlen(checksum), sink1->fd);
    fflush(sink1->fd);
 } else {
    r_hash = (char *) malloc(sizeof(char) * 100);
    if( fgets(r_hash, 100, sink1->fd) != 0 ) {
      if(! strstr(checksum, r_hash) ) {
        if(!sink1->status)
       {
          g_print("Checksum mismatch at frame %d!\n", count++);
          sink1->status++;
       }
      }
    } else {
      if(!sink1->status)
     {
        g_print("Error: Checksum file has only %d frames!\n", count - 1);
        sink1->status++;
      }
    }
   free(r_hash);
  }

  g_free (checksum);

  return GST_FLOW_OK;
}
