/*
 * In-process synchronization primitives
 *
 * Copyright (C) 2021-2022 Elizabeth Figura for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"
#include "handle.h"
#include "request.h"
#include "thread.h"

#if defined(__linux__)
# include "ntsync_tmp.h"
#endif

static int device = -1;

int use_inproc_sync(void)
{
    static int enabled = -1;
    const char *env;

    if (enabled != -1) return enabled;
    enabled = 0;
    if ((env = getenv( "PROTON_NO_NTSYNC" )) && atoi( env )) return 0;
#if defined(NTSYNC_IOC_EVENT_READ)
    device = open( "/dev/ntsync", O_RDONLY | O_CLOEXEC );
    enabled = device != -1;
#endif
    return enabled;
}

void close_inproc_sync( int obj )
{
    if (obj == -1) return;
    close( obj );
}

int create_inproc_event( int manual_reset, int signaled )
{
    int obj = -1;
    if (!use_inproc_sync()) return -1;
#ifdef NTSYNC_IOC_EVENT_READ
    {
        struct ntsync_event_args args = { .manual = manual_reset, .signaled = signaled };
        obj = ioctl( device, NTSYNC_IOC_CREATE_EVENT, &args );
    }
#endif
    if (obj == -1) fatal_error( "Cannot allocate ntsync event\n" );
    return obj;
}

int create_inproc_semaphore( unsigned int count, unsigned int max )
{
    int obj = -1;
    if (!use_inproc_sync()) return -1;
#ifdef NTSYNC_IOC_EVENT_READ
    {
        struct ntsync_sem_args args = { .count = count, .max = max };
        obj = ioctl( device, NTSYNC_IOC_CREATE_SEM, &args );
    }
#endif
    if (obj == -1) fatal_error( "Cannot allocate ntsync semaphore\n" );
    return obj;
}

int create_inproc_mutex( thread_id_t owner, unsigned int count )
{
    int obj = -1;
    if (!use_inproc_sync()) return -1;
#ifdef NTSYNC_IOC_EVENT_READ
    {
        struct ntsync_mutex_args args = { .owner = owner, .count = count };
        obj = ioctl( device, NTSYNC_IOC_CREATE_MUTEX, &args );
    }
#endif
    if (obj == -1) fatal_error( "Cannot allocate ntsync mutex\n" );
    return obj;
}

void set_inproc_event( int event )
{
    uint32_t prev;
    if (!use_inproc_sync() || event == -1) return;
#if defined(NTSYNC_IOC_EVENT_READ)
    ioctl( event, NTSYNC_IOC_EVENT_SET, &prev );
#endif
}

void reset_inproc_event( int event )
{
    uint32_t prev;
    if (!use_inproc_sync() || event == -1) return;
#if defined(NTSYNC_IOC_EVENT_READ)
    ioctl( event, NTSYNC_IOC_EVENT_RESET, &prev );
#endif
}

void abandon_inproc_mutex( thread_id_t tid, int mutex )
{
    if (!use_inproc_sync() || mutex == -1) return;
#if defined(NTSYNC_IOC_EVENT_READ)
    ioctl( mutex, NTSYNC_IOC_MUTEX_KILL, &tid );
#endif
}

DECL_HANDLER(get_linux_sync_device)
{
    if (!use_inproc_sync()) set_error( STATUS_NOT_IMPLEMENTED );
    else send_client_fd( current->process, device, 0 );
}

DECL_HANDLER(get_linux_sync_obj)
{
    struct object *obj;
    enum inproc_sync_type type = INPROC_SYNC_UNKNOWN;
    int id;

    if (!use_inproc_sync())
    {
        set_error( STATUS_NOT_IMPLEMENTED );
        return;
    }
    if (!(obj = get_handle_obj( current->process, req->handle, 0, NULL ))) return;
    reply->access = get_handle_access( current->process, req->handle );
    if ((id = obj->ops->get_inproc_sync( obj, &type )) == -1)
        set_error( STATUS_NOT_IMPLEMENTED );
    else
    {
        reply->type = type;
        send_client_fd( current->process, id, req->handle );
    }
    release_object( obj );
}
