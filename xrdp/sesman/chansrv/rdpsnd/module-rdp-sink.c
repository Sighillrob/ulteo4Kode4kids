/* $Id: module-rdp-sink.c 2043 2007-11-09 18:25:40Z lennart $ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

/**
 * Copyright (C) 2010 Ulteo SAS
 * http://www.ulteo.com
 * Author David Lechavalier <david@ulteo.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <pulsecore/sink.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/rtclock.h>

#include "module-rdp-sink-symdef.h"
#include "sound_channel.h"

PA_MODULE_AUTHOR("David LECHEVALIER");
PA_MODULE_DESCRIPTION("Ulteo RDP sink");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        "format=<sample format> "
        "channels=<number of channels> "
        "rate=<sample rate> "
        "sink_name=<name of sink>"
        "channel_map=<channel map>"
        "description=<description for the sink>");

#define DEFAULT_SINK_NAME "rdp"

extern int completion_count;

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_sink *sink;

    pa_thread *thread;
    pa_thread *threadv;
    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;
    pa_memchunk memchunk;

    size_t block_size;
    int fd;
    struct timeval timestamp;
};

static const char* const valid_modargs[] = {
    "rate",
    "format",
    "channels",
    "sink_name",
    "channel_map",
    "description",
    NULL
};

static int sink_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SINK(o)->userdata;

    switch (code) {
        case PA_SINK_MESSAGE_SET_STATE:
            pa_log_debug("module-rdp[sink_process_msg]: "
            		"Process message: PA_SINK_MESSAGE_SET_STATE");
            if (PA_PTR_TO_UINT(data) == PA_SINK_RUNNING)
                pa_rtclock_get(&u->timestamp);

            break;

        case PA_SINK_MESSAGE_GET_LATENCY: {
          pa_log_debug("module-rdp[sink_process_msg]: "
          		"Process message: PA_SINK_MESSAGE_GET_LATENCY");
          struct timeval now;

            pa_rtclock_get(&now);

            if (pa_timeval_cmp(&u->timestamp, &now) > 0)
                *((pa_usec_t*) data) = 0;
            else
                *((pa_usec_t*) data) = pa_timeval_diff(&u->timestamp, &now);
            break;
        }
    }

    return pa_sink_process_msg(o, code, data, offset, chunk);
}



void pa_sink_process(int stamp, int fd, pa_sink *s, size_t length) {
    pa_sink_input *i;
    void *state = NULL;
    int write_type = 0;
    int packet_id=0;

    pa_sink_assert_ref(s);
    pa_assert(PA_SINK_OPENED(s->thread_info.state));
    pa_assert(length > 0);
    pa_assert(pa_frame_aligned(length, &s->sample_spec));

    pa_memchunk chunk;
    /* If something is connected to our monitor source, we have to
     * pass valid data to it */
    void *p;
    int current_size=0;
    pa_log_debug("module-rdp[pa_sink_process]: "
    		"Send wave Frame ");

    if (completion_count > 20)
    {
//      while (length > 0) {
//      	pa_sink_render(s, length, &chunk);
//      	p = pa_memblock_acquire(chunk.memblock);
//      	pa_memblock_release(chunk.memblock);
//      	pa_memblock_unref(chunk.memblock);
//      	pa_assert(chunk.length <= length);
//      	length -= chunk.length;
//      }

    	return;
    }
    while (length > 0) {
    	pa_sink_render(s, length, &chunk);
    	p = pa_memblock_acquire(chunk.memblock);

      if(chunk.length != 0 )
      {
      	vchannel_sound_send_wave_info(stamp, chunk.length, (char*) p + chunk.index);
      	completion_count++;
      }
    	pa_memblock_release(chunk.memblock);
    	pa_memblock_unref(chunk.memblock);
    	pa_assert(chunk.length <= length);
    	length -= chunk.length;
    }
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;
    void *p;
    int write_type = 0;

    pa_assert(u);

    pa_log_debug("module-rdp[thread_func]: "
    		"Thread starting up");

    pa_thread_mq_install(&u->thread_mq);
    pa_rtpoll_install(u->rtpoll);

    pa_rtclock_get(&u->timestamp);

    for (;;) {
        int ret;
        int l;
        /* Render some data and drop it immediately */
        if (u->sink->thread_info.state == PA_SINK_RUNNING) {
            struct timeval now;

            pa_rtclock_get(&now);

            if (pa_timeval_cmp(&u->timestamp, &now) <= 0) {
              pa_sink_process((int)u->timestamp.tv_sec ,u->fd, u->sink, u->block_size);
            	pa_timeval_add(&u->timestamp, pa_bytes_to_usec(u->block_size, &u->sink->sample_spec));
            }

            pa_rtpoll_set_timer_absolute(u->rtpoll, &u->timestamp);
        } else
            pa_rtpoll_set_timer_disabled(u->rtpoll);

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll, 1)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("module-rdp[thread_func]: "
    		"Thread shutting down");
}

int pa__init(pa_module*m) {
    struct userdata *u = NULL;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_modargs *ma = NULL;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log_error("module-rdp[pa_init]: "
        		"Failed to parse module arguments.");
        goto fail;
    }

    ss = m->core->default_sample_spec;
    if (pa_modargs_get_sample_spec_and_channel_map(ma, &ss, &map, PA_CHANNEL_MAP_DEFAULT) < 0) {
        pa_log_error("module-rdp[pa_init]: "
        		"Invalid sample format specification or channel map");
        goto fail;
    }

    u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    m->userdata = u;
    pa_thread_mq_init(&u->thread_mq, m->core->mainloop);
    u->rtpoll = pa_rtpoll_new();
    pa_rtpoll_item_new_asyncmsgq(u->rtpoll, PA_RTPOLL_EARLY, u->thread_mq.inq);

    if (!(u->sink = pa_sink_new(m->core, __FILE__, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME), 0, &ss, &map))) {
        pa_log_error("module-rdp[pa_init]: "
        		"Failed to create sink.");
        goto fail;
    }

    u->sink->parent.process_msg = sink_process_msg;
    u->sink->userdata = u;
    u->sink->flags = PA_SINK_LATENCY;

    pa_sink_set_module(u->sink, m);
    pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);
    pa_sink_set_rtpoll(u->sink, u->rtpoll);
    pa_sink_set_description(u->sink, pa_modargs_get_value(ma, "description", "RDP sink"));

    u->block_size = pa_bytes_per_second(&ss) / 20; /* 50 ms */
    if (u->block_size <= 0)
        u->block_size = pa_frame_size(&ss);

    pa_log_debug("module-rdp[pa_init]: "
    		"Sample : %s",pa_sample_format_to_string(ss.format));
    pa_log_debug("module-rdp[pa_init]: "
    		"Rate : %i", ss.rate);
    pa_log_debug("Channel : %i", ss.channels);

    pa_log_debug("module-rdp[pa_init]: "
    		"Initialise channel");
    init_channel();

    pa_log_debug("module-rdp[pa_init]: "
    		"Create sound thread");
    if (!(u->thread = pa_thread_new(thread_func, u))) {
        pa_log_error("module-rdp[pa_init]: "
        		"Failed to create thread.");
        goto fail;
    }

    pa_log_debug("module-rdp[pa_init]: "
    		"Create virtual channel thread");
    if (!(u->threadv = pa_thread_new(thread_vchannel_process, u))) {
        pa_log_error("module-rdp[pa_init]: "
        		"Failed to create thread.");
        goto fail;
    }
    pa_sink_put(u->sink);

    pa_modargs_free(ma);
    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;
    pa_log_debug("module-rdp[pa_done]: ");
    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    pa_xfree(u);
}
