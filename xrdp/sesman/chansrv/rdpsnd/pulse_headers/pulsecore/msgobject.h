#ifndef foopulsemsgobjecthfoo
#define foopulsemsgobjecthfoo

/* $Id: msgobject.h 1971 2007-10-28 19:13:50Z lennart $ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <sys/types.h>

#include <pulse/xmalloc.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/macro.h>
#include <pulsecore/object.h>
#include <pulsecore/memchunk.h>

typedef struct pa_msgobject pa_msgobject;

struct pa_msgobject {
    pa_object parent;
    int (*process_msg)(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);
};

pa_msgobject *pa_msgobject_new_internal(size_t size, const char *type_name, int (*check_type)(const char *type_name));

int pa_msgobject_check_type(const char *type);

#define pa_msgobject_new(type) ((type*) pa_msgobject_new_internal(sizeof(type), #type, type##_check_type))
#define pa_msgobject_free ((void (*) (pa_msgobject* o)) pa_object_free)

#define PA_MSGOBJECT(o) pa_msgobject_cast(o)

PA_DECLARE_CLASS(pa_msgobject);

#endif
