/*
 * Copyright (c) [2021] Texas Instruments Incorporated
 *
 * All rights reserved not granted herein.
 *
 * Limited License.
 *
 * Texas Instruments Incorporated grants a world-wide, royalty-free,
 * non-exclusive license under copyrights and patents it now or hereafter
 * owns or controls to make, have made, use, import, offer to sell and sell
 * ("Utilize") this software subject to the terms herein.  With respect to
 * the foregoing patent license, such license is granted  solely to the extent
 * that any such patent is necessary to Utilize the software alone.
 * The patent license shall not apply to any combinations which include
 * this software, other than combinations with devices manufactured by or
 * for TI (“TI Devices”).  No hardware patent is licensed hereunder.
 *
 * Redistributions must preserve existing copyright notices and reproduce
 * this license (including the above copyright notice and the disclaimer
 * and (if applicable) source code license limitations below) in the
 * documentation and/or other materials provided with the distribution
 *
 * Redistribution and use in binary form, without modification, are permitted
 * provided that the following conditions are met:
 *
 * *	No reverse engineering, decompilation, or disassembly of this software
 *      is permitted with respect to any software provided in binary form.
 *
 * *	Any redistribution and use are licensed by TI for use only with TI
 * Devices.
 *
 * *	Nothing shall obligate TI to provide you with source code for the
 *      software licensed and provided to you in object code.
 *
 * If software source code is provided to you, modification and redistribution
 * of the source code are permitted provided that the following conditions are
 * met:
 *
 * *	Any redistribution and use of the source code, including any resulting
 *      derivative works, are licensed by TI for use only with TI Devices.
 *
 * *	Any redistribution and use of any object code compiled from the source
 *      code and any resulting derivative works, are licensed by TI for use
 *      only with TI Devices.
 *
 * Neither the name of Texas Instruments Incorporated nor the names of its
 * suppliers may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * DISCLAIMER.
 *
 * THIS SOFTWARE IS PROVIDED BY TI AND TI’S LICENSORS "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TI AND TI’S LICENSORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gsttiovxpad.h"

#include "gsttiovxbufferpool.h"

/**
 * SECTION:gsttiovxpad
 * @short_description: GStreamer pad for GstTIOVX based elements
 *
 * This class implements a GStreamer standard buffer pool for GstTIOVX
 * based elements.
 */

GST_DEBUG_CATEGORY_STATIC (gst_tiovx_pad_debug_category);
#define GST_CAT_DEFAULT gst_tiovx_pad_debug_category

struct _GstTIOVXPad
{
  GstPad base;

  GstTIOVXBufferPool *buffer_pool;


    gboolean (*notify_function) (GstElement * notify_element);
  GstElement *notify_element;
};

G_DEFINE_TYPE_WITH_CODE (GstTIOVXPad, gst_tiovx_pad,
    GST_TYPE_PAD,
    GST_DEBUG_CATEGORY_INIT (gst_tiovx_pad_debug_category,
        "tiovxpad", 0, "debug category for TIOVX pad class"));

/* prototypes */
static gboolean gst_tiovx_pad_query_func (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void gst_tiovx_pad_finalize (GObject * object);

static void
gst_tiovx_pad_class_init (GstTIOVXPadClass * klass)
{
  GObjectClass *o_class = G_OBJECT_CLASS (klass);

  o_class->finalize = gst_tiovx_pad_finalize;
}

static void
gst_tiovx_pad_init (GstTIOVXPad * this)
{
  this->buffer_pool = NULL;

  gst_pad_set_query_function ((GstPad *) this, gst_tiovx_pad_query_func);
}

#define MIN_BUFFERS 2
#define MAX_BUFFERS 8
#define SIZE 1000000

gboolean
gst_tiovx_pad_trigger (GstPad * pad, GstCaps * caps)
{
  GstTIOVXPad *tiovx_pad = GST_TIOVX_PAD (pad);
  GstQuery *query = NULL;
  gint npool = 0;
  gboolean ret = FALSE;

  g_return_val_if_fail (pad, FALSE);

  query = gst_query_new_allocation (caps, TRUE);

  ret = gst_pad_query (pad, query);
  if (!ret) {
    goto unref_query;
  }

  /* Always remove the current pool, we will either create a new one or get it from downstream */
  gst_object_unref (tiovx_pad->buffer_pool);
  tiovx_pad->buffer_pool = NULL;

  /* Look for the first TIOVX buffer if present */
  for (npool = 0; npool < gst_query_get_n_allocation_pools (query); ++npool) {
    GstBufferPool *pool;

    gst_query_parse_nth_allocation_pool (query, npool, &pool, NULL, NULL, NULL);

    if (GST_TIOVX_IS_BUFFER_POOL (pool)) {
      tiovx_pad->buffer_pool = GST_TIOVX_BUFFER_POOL (pool);
      break;
    }
  }

  if (NULL == tiovx_pad->buffer_pool) {
    tiovx_pad->buffer_pool = g_object_new (GST_TIOVX_TYPE_BUFFER_POOL, NULL);
  }

  ret = TRUE;

unref_query:
  gst_query_unref (query);

  return ret;
}

void
gst_tiovx_pad_install_notify (GstPad * pad,
    gboolean (*notify_function) (GstElement * element), GstElement * element)
{
  GstTIOVXPad *self = GST_TIOVX_PAD (pad);

  self->notify_function = notify_function;
  self->notify_element = element;
}

static gboolean
gst_tiovx_pad_process_allocation_query (GstPad * pad, GstQuery * query)
{
  GstTIOVXPad *tiovx_pad = GST_TIOVX_PAD (pad);
  gboolean ret = FALSE;

  g_return_val_if_fail (pad, FALSE);
  g_return_val_if_fail (query, FALSE);

  if (NULL != tiovx_pad->buffer_pool) {
    gst_object_unref (tiovx_pad->buffer_pool);
    goto out;
  }

  tiovx_pad->buffer_pool = g_object_new (GST_TIOVX_TYPE_BUFFER_POOL, NULL);

  gst_query_add_allocation_pool (query,
      GST_BUFFER_POOL (tiovx_pad->buffer_pool), SIZE, MIN_BUFFERS, MAX_BUFFERS);

  ret = TRUE;

out:
  return ret;
}

static gboolean
gst_tiovx_pad_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstTIOVXPad *tiovx_pad = GST_TIOVX_PAD (pad);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      GST_DEBUG_OBJECT (pad, "Received allocation query");
      if ((NULL != tiovx_pad->notify_function)
          || (NULL != tiovx_pad->notify_element)) {
        /* Notify the TIOVX element that it can start the downstream query allocation */
        tiovx_pad->notify_function (tiovx_pad->notify_element);

        /* Start this pad's allocation query */
        ret = gst_tiovx_pad_process_allocation_query (pad, query);
      } else {
        GST_ERROR_OBJECT (pad,
            "No notify function or element to notify installed");
      }
      break;
    default:
      GST_DEBUG_OBJECT (pad, "Received non-allocation query");
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static void
gst_tiovx_pad_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_tiovx_pad_parent_class)->finalize (object);
}
