/* -*- Mode: C; indent-tabs-mode:t ; c-basic-offset:8 -*- */
/*
 * Hotplug functions for libusb
 * Copyright © 2012-2013 Nathan Hjelm <hjelmn@mac.com>
 * Copyright © 2012-2013 Peter Stuge <peter@stuge.se>
 * Copyright © 2014-2020 Chris Dickens <christopher.a.dickens@gmail.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libusbi.h"

/**
 * @defgroup libusb_hotplug Device hotplug event notification
 * This page details how to use the libusb hotplug interface, where available.
 *
 * Be mindful that not all platforms currently implement hotplug notification and
 * that you should first call on libusb_has_capability() with parameter
 * \ref LIBUSB_CAP_HAS_HOTPLUG to confirm that hotplug support is available.
 *
 * \page libusb_hotplug Device hotplug event notification
 *
 * \section hotplug_intro Introduction
 *
 * Version 1.0.16, \ref LIBUSB_API_VERSION >= 0x01000102, has added support
 * for hotplug events on <b>some</b> platforms (you should test if your platform
 * supports hotplug notification by calling libusb_has_capability() with
 * parameter \ref LIBUSB_CAP_HAS_HOTPLUG).
 *
 * This interface allows you to request notification for the arrival and departure
 * of matching USB devices.
 *
 * To receive hotplug notification you register a callback by calling
 * libusb_hotplug_register_callback(). This function will optionally return
 * a callback handle that can be passed to libusb_hotplug_deregister_callback().
 *
 * A callback function must return an integer indicating whether the callback is
 * expecting additional events. Returning 0 will rearm the callback and anything
 * else will cause the callback to be deregistered. Note that when callbacks are
 * called from libusb_hotplug_register_callback() because of the \ref LIBUSB_HOTPLUG_ENUMERATE
 * flag, the callback return value is ignored. In other words, you cannot cause a
 * callback to be deregistered by returning a non-zero value when it is called from
 * libusb_hotplug_register_callback().
 *
 * Callbacks for a particular context are automatically deregistered by libusb_exit().
 *
 * As of version 1.0.16 there are two supported hotplug events:
 *  - \ref LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED : A device has been plugged in and is ready to use
 *  - \ref LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT : A device has left and is no longer available
 *
 * A hotplug callback can receive either or both of these events.
 *
 * \note
 * If you receive notification that a device has left and you have any
 * \ref libusb_device_handle "libusb_device_handles" for the device, it is up to you to
 * call libusb_close() on each device handle to free up any remaining resources
 * associated with the device. Once a device has left any
 * \ref libusb_device_handle "libusb_device_handles" associated with the device are
 * invalid and will remain so even if the device comes back.
 *
 * When handling a \ref LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED event it is considered
 * safe to call any libusb function that takes a libusb_device. It also safe to
 * open a device and submit asynchronous transfers. However, most other functions
 * that take a \ref libusb_device_handle are <b>not</b> safe to call. Examples of such
 * functions are any of the \ref libusb_syncio "synchronous API" functions or the blocking
 * functions that retrieve various \ref libusb_desc "USB descriptors". These functions must
 * be used outside of the context of the hotplug callback.
 *
 * When handling a \ref LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT event the only safe function
 * is libusb_get_device_descriptor().
 *
 * The following code provides an example of the usage of the hotplug interface:
\code
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libusb.h>

static int count = 0;

int LIBUSB_CALL hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev,
                                 libusb_hotplug_event event, void *user_data)
{
  static libusb_device_handle *dev_handle = NULL;
  struct libusb_device_descriptor desc;
  int rc;

  (void)libusb_get_device_descriptor(dev, &desc);

  if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event) {
    rc = libusb_open(dev, &dev_handle);
    if (LIBUSB_SUCCESS != rc) {
      printf("Could not open USB device\n");
    }
  } else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event) {
    if (dev_handle) {
      libusb_close(dev_handle);
      dev_handle = NULL;
    }
  } else {
    printf("Unhandled event %d\n", event);
  }
  count++;

  return 0;
}

int main(void)
{
  libusb_hotplug_callback_handle callback_handle;
  int rc;

  libusb_init(NULL);

  rc = libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                        LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0, 0x045a, 0x5005,
                                        LIBUSB_HOTPLUG_MATCH_ANY, hotplug_callback, NULL,
                                        &callback_handle);
  if (LIBUSB_SUCCESS != rc) {
    printf("Error creating a hotplug callback\n");
    libusb_exit(NULL);
    return EXIT_FAILURE;
  }

  while (count < 2) {
    libusb_handle_events_completed(NULL, NULL);
    nanosleep(&(struct timespec){0, 10000000UL}, NULL);
  }

  libusb_hotplug_deregister_callback(NULL, callback_handle);
  libusb_exit(NULL);

  return 0;
}
\endcode
 */

#define VALID_HOTPLUG_EVENTS			\
	(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |	\
	 LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)

#define VALID_HOTPLUG_FLAGS			\
	(LIBUSB_HOTPLUG_ENUMERATE)

static void free_hotplug_cb(struct usbi_hotplug_callback *hotplug_cb)
{
	usbi_dbg("freeing hotplug cb %p with handle %d", hotplug_cb, hotplug_cb->handle);

	if (hotplug_cb->notifications) {
		struct usbi_hotplug_notification *notification;
		unsigned int n;

		/* release the device reference for each notification */
		for (n = 0; n < hotplug_cb->num_notifications; n++) {
			notification = &hotplug_cb->notifications[n];
			libusb_unref_device(notification->device);
		}

		free(hotplug_cb->notifications);
	}

	list_del(&hotplug_cb->list);
	free(hotplug_cb);
}

void usbi_hotplug_init(struct libusb_context *ctx)
{
	if (!usbi_backend_has_hotplug())
		return;

	list_init(&ctx->hotplug_cbs);
	ctx->next_hotplug_cb_handle = 1;
}

void usbi_hotplug_exit(struct libusb_context *ctx)
{
	struct usbi_hotplug_callback *hotplug_cb, *next_hotplug_cb;
	struct libusb_device *dev, *next_dev;

	if (!usbi_backend_has_hotplug())
		return;

	for_each_hotplug_cb_safe(ctx, hotplug_cb, next_hotplug_cb)
		free_hotplug_cb(hotplug_cb);

	for_each_device_safe(ctx, dev, next_dev) {
		list_del(&dev->list);
		libusb_unref_device(dev);
	}
}

static int hotplug_cb_match(struct usbi_hotplug_callback *hotplug_cb,
	struct libusb_device *dev, libusb_hotplug_event event)
{
	struct libusb_device_descriptor *desc = &dev->device_descriptor;

	if (!(hotplug_cb->flags & event))
		return 0;

	if ((hotplug_cb->flags & USBI_HOTPLUG_VENDOR_ID_VALID) &&
	    hotplug_cb->vendor_id != desc->idVendor)
		return 0;

	if ((hotplug_cb->flags & USBI_HOTPLUG_PRODUCT_ID_VALID) &&
	    hotplug_cb->product_id != desc->idProduct)
		return 0;

	if ((hotplug_cb->flags & USBI_HOTPLUG_DEV_CLASS_VALID) &&
	    hotplug_cb->dev_class != desc->bDeviceClass)
		return 0;

	return 1;
}

static int hotplug_notification(struct libusb_context *ctx, struct libusb_device *dev,
	libusb_hotplug_event event)
{
	struct usbi_hotplug_callback *hotplug_cb;
	int notified = 0;

	/* iterate through the registered hotplug callbacks and deliver this message
	 * to any interested callbacks */
	for_each_hotplug_cb(ctx, hotplug_cb) {
		struct usbi_hotplug_notification *new_notifications;

		if (!hotplug_cb_match(hotplug_cb, dev, event)) {
			/* callback is not interested in this event/device */
			continue;
		}

		new_notifications = realloc(hotplug_cb->notifications,
					    (hotplug_cb->num_notifications + 1) * sizeof(*new_notifications));
		if (!new_notifications) {
			usbi_err(ctx, "error allocating hotplug notification for handle %d", hotplug_cb->handle);
			continue;
		}
		hotplug_cb->notifications = new_notifications;

		/* increase device refcnt and add a new notification to this hotplug callback */
		hotplug_cb->notifications[hotplug_cb->num_notifications].event = event;
		hotplug_cb->notifications[hotplug_cb->num_notifications].device = libusb_ref_device(dev);
		hotplug_cb->num_notifications++;

		notified = 1;
	}

	return notified;
}

void usbi_connect_device(struct libusb_device *dev)
{
	struct libusb_context *ctx = DEVICE_CTX(dev);
	int notified = 0;

	usbi_atomic_store(&dev->attached, 1);

	usbi_mutex_lock(&ctx->usb_devs_lock);
	list_add(&dev->list, &ctx->usb_devs);
	if (usbi_backend_has_hotplug())
		notified = hotplug_notification(ctx, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
	usbi_mutex_unlock(&ctx->usb_devs_lock);

	if (notified) {
		/* signal that one or more hotplug callbacks got this notification */
		usbi_set_event_flag(ctx, USBI_EVENT_HOTPLUG_NOTIFICATION_PENDING);
	}
}

void usbi_disconnect_device(struct libusb_device *dev)
{
	struct libusb_context *ctx = DEVICE_CTX(dev);
	int notified = 0;

	usbi_atomic_store(&dev->attached, 0);

	usbi_mutex_lock(&ctx->usb_devs_lock);
	list_del(&dev->list);
	if (usbi_backend_has_hotplug()) {
		notified = hotplug_notification(ctx, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);

		/* unreference the device, any interested hotplug callbacks will
		 * hold a reference to it until the notification is processed or
		 * the hotplug callback is deregistered */
		libusb_unref_device(dev);
	}
	usbi_mutex_unlock(&ctx->usb_devs_lock);

	if (notified) {
		/* signal that one or more hotplug callbacks got this notification */
		usbi_set_event_flag(ctx, USBI_EVENT_HOTPLUG_NOTIFICATION_PENDING);
	}
}

void usbi_hotplug_process_notifications(struct libusb_context *ctx)
{
	struct usbi_hotplug_callback *hotplug_cb, *next_hotplug_cb;

	usbi_set_event_handling_flag(ctx, USBI_HANDLING_HOTPLUG_NOTIFICATIONS);

	usbi_mutex_lock(&ctx->usb_devs_lock);

	for_each_hotplug_cb_safe(ctx, hotplug_cb, next_hotplug_cb) {
		struct usbi_hotplug_notification *notification;
		unsigned int n;
		int ret = 0;

		if (!hotplug_cb->notifications) {
			/* nothing pending for this callback */
			continue;
		}

		for (n = 0; !ret && n < hotplug_cb->num_notifications; n++) {
			if (hotplug_cb->flags & USBI_HOTPLUG_NEEDS_FREE) {
				/* this callback was deregistered, stop processing */
				break;
			}

			notification = &hotplug_cb->notifications[n];
			ret = hotplug_cb->callback(ctx, notification->device, notification->event, hotplug_cb->user_data);
		}

		if (ret) {
			/* this callback indicated it is done, free it */
			free_hotplug_cb(hotplug_cb);
			continue;
		}

		/* this callback is still active, unref the devices */
		for (n = 0; n < hotplug_cb->num_notifications; n++) {
			notification = &hotplug_cb->notifications[n];
			libusb_unref_device(notification->device);
		}

		/* and reset the notifications list */
		free(hotplug_cb->notifications);
		hotplug_cb->notifications = NULL;
		hotplug_cb->num_notifications = 0;
	}

	if (usbi_test_event_handling_flag(ctx, USBI_HOTPLUG_CB_FREED)) {
		for_each_hotplug_cb_safe(ctx, hotplug_cb, next_hotplug_cb) {
			if (hotplug_cb->flags & USBI_HOTPLUG_NEEDS_FREE)
				free_hotplug_cb(hotplug_cb);
		}
	}

	usbi_mutex_unlock(&ctx->usb_devs_lock);

	usbi_clear_event_handling_flag(ctx, USBI_HANDLING_HOTPLUG_NOTIFICATIONS);
}

/** \ingroup libusb_hotplug
 * Register a hotplug callback function. The callback will fire when a
 * matching event occurs on a matching device. The callback remains active
 * until either it is deregistered with libusb_hotplug_deregister_callback()
 * or the supplied callback returns non-zero to indicate it is finished
 * processing events.
 *
 * If \ref LIBUSB_HOTPLUG_ENUMERATE is passed in flags, the callback will be
 * called with \ref LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED for all devices
 * currently plugged into the machine. The return value of the callback is
 * ignored during this time.
 *
 * Since version 1.0.16, \ref LIBUSB_API_VERSION >= 0x01000102
 *
 * \param[in] ctx context to register this callback with
 * \param[in] events bitwise OR of hotplug events that will trigger this callback.
 *            See \ref libusb_hotplug_event
 * \param[in] flags bitwise OR of hotplug flags that affect registration.
 *            See \ref libusb_hotplug_registration_flag
 * \param[in] vendor_id the vendor id to match or \ref LIBUSB_HOTPLUG_MATCH_ANY
 * \param[in] product_id the product id to match or \ref LIBUSB_HOTPLUG_MATCH_ANY
 * \param[in] dev_class the device class to match or \ref LIBUSB_HOTPLUG_MATCH_ANY
 * \param[in] callback the function to be invoked on a matching event/device
 * \param[in] user_data user data pointer to pass to the callback function
 * \param[out] callback_handle pointer to store the handle of the allocated callback (can be NULL)
 * \returns \ref LIBUSB_SUCCESS, or a \ref libusb_error "LIBUSB_ERROR" code on failure
 */
int API_EXPORTED libusb_hotplug_register_callback(libusb_context *ctx,
	int events, int flags,
	int vendor_id, int product_id, int dev_class,
	libusb_hotplug_callback_fn callback, void *user_data,
	libusb_hotplug_callback_handle *callback_handle)
{
	struct usbi_hotplug_callback *hotplug_cb;
	libusb_hotplug_callback_handle _callback_handle;

	/* check for sane values */
	if (!events || (events & ~VALID_HOTPLUG_EVENTS) || (flags & ~VALID_HOTPLUG_FLAGS) ||
	    (vendor_id != LIBUSB_HOTPLUG_MATCH_ANY && (vendor_id & ~0xffff)) ||
	    (product_id != LIBUSB_HOTPLUG_MATCH_ANY && (product_id & ~0xffff)) ||
	    (dev_class != LIBUSB_HOTPLUG_MATCH_ANY && (dev_class & ~0xff)) ||
	    !callback) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	/* check for hotplug support */
	if (!usbi_backend_has_hotplug())
		return LIBUSB_ERROR_NOT_SUPPORTED;

	hotplug_cb = malloc(sizeof(*hotplug_cb));
	if (!hotplug_cb)
		return LIBUSB_ERROR_NO_MEM;

	hotplug_cb->flags = (uint8_t)events;
	if (vendor_id != LIBUSB_HOTPLUG_MATCH_ANY) {
		hotplug_cb->flags |= USBI_HOTPLUG_VENDOR_ID_VALID;
		hotplug_cb->vendor_id = (uint16_t)vendor_id;
	}
	if (product_id != LIBUSB_HOTPLUG_MATCH_ANY) {
		hotplug_cb->flags |= USBI_HOTPLUG_PRODUCT_ID_VALID;
		hotplug_cb->product_id = (uint16_t)product_id;
	}
	if (dev_class != LIBUSB_HOTPLUG_MATCH_ANY) {
		hotplug_cb->flags |= USBI_HOTPLUG_DEV_CLASS_VALID;
		hotplug_cb->dev_class = (uint8_t)dev_class;
	}
	hotplug_cb->callback = callback;
	hotplug_cb->user_data = user_data;
	hotplug_cb->notifications = NULL;
	hotplug_cb->num_notifications = 0;

	ctx = usbi_get_context(ctx);

	/* we hold usb_devs_lock for the duration of the callback registration,
	 * including the initial device enumeration if LIBUSB_HOTPLUG_ENUMERATE
	 * is specified, thus ensuring that the callback will not receive any
	 * events for a device which is not currently in the usb_devs list */
	usbi_mutex_lock(&ctx->usb_devs_lock);

	/* protect the handle by the context usb_devs_lock */
	hotplug_cb->handle = _callback_handle = ctx->next_hotplug_cb_handle++;

	/* handle the unlikely case of overflow */
	if (ctx->next_hotplug_cb_handle < 0) {
		usbi_warn(ctx, "hotplug handle overflow");
		ctx->next_hotplug_cb_handle = 1;
	}

	usbi_dbg("new hotplug cb %p with handle %d", hotplug_cb, _callback_handle);

	if ((flags & LIBUSB_HOTPLUG_ENUMERATE) && (events & LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)) {
		struct libusb_device *dev;

		for_each_device(ctx, dev) {
			if (!hotplug_cb_match(hotplug_cb, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED))
				continue;
			(void)callback(ctx, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, user_data);
		}
	}

	/* add to tail so that callbacks are invoked in FIFO order */
	list_add_tail(&hotplug_cb->list, &ctx->hotplug_cbs);

	usbi_mutex_unlock(&ctx->usb_devs_lock);

	if (callback_handle)
		*callback_handle = _callback_handle;

	return LIBUSB_SUCCESS;
}

/** \ingroup libusb_hotplug
 * Deregister a hotplug callback. This function is safe to call from within
 * a hotplug callback.
 *
 * Since version 1.0.16, \ref LIBUSB_API_VERSION >= 0x01000102
 *
 * \param[in] ctx context this callback is registered with
 * \param[in] callback_handle the handle of the callback to deregister
 */
void API_EXPORTED libusb_hotplug_deregister_callback(libusb_context *ctx,
	libusb_hotplug_callback_handle callback_handle)
{
	struct usbi_hotplug_callback *hotplug_cb;
	int found = 0;

	/* check for hotplug support */
	if (!usbi_backend_has_hotplug())
		return;

	usbi_dbg("deregister hotplug cb %d", callback_handle);

	ctx = usbi_get_context(ctx);

	usbi_mutex_lock(&ctx->usb_devs_lock);
	for_each_hotplug_cb(ctx, hotplug_cb) {
		if (hotplug_cb->handle != callback_handle)
			continue;

		/* the correct execution path depends on whether we are being called from
		 * a hotplug callback or not. in the former case, freeing of the hotplug
		 * callback is deferred until hotplug notifications are finished being
		 * processed, but this callback will not be invoked any longer even if
		 * there are still pending notifications. */
		if (usbi_test_event_handling_flag(ctx, USBI_HANDLING_HOTPLUG_NOTIFICATIONS)) {
			/* set this flag so that usbi_hotplug_process_notifications()
			 * can free the callback once all notifications are processed */
			usbi_set_event_handling_flag(ctx, USBI_HOTPLUG_CB_FREED);
			hotplug_cb->flags |= USBI_HOTPLUG_NEEDS_FREE;
		} else {
			free_hotplug_cb(hotplug_cb);
		}

		found = 1;
		break;
	}
	usbi_mutex_unlock(&ctx->usb_devs_lock);

	if (found && !usbi_handling_events(ctx)) {
		/* since we aren't being called by the event handler, we need to flag
		 * that a hotplug callback was just deregistered in order to preserve
		 * the original behavior that this action would interrupt any active
		 * event handler */
		usbi_set_event_flag(ctx, USBI_EVENT_HOTPLUG_CB_DEREGISTERED);
	}
}

/** \ingroup libusb_hotplug
 * Gets the user data associated with a hotplug callback.
 *
 * Since version v1.0.24, \ref LIBUSB_API_VERSION >= 0x01000108
 *
 * \param[in] ctx context this callback is registered with
 * \param[in] callback_handle the handle of the callback to get the user data of
 * \returns the user data, or NULL if the hotplug callback handle is invalid
 */
DEFAULT_VISIBILITY
void * LIBUSB_CALL libusb_hotplug_get_user_data(libusb_context *ctx,
	libusb_hotplug_callback_handle callback_handle)
{
	struct usbi_hotplug_callback *hotplug_cb;
	void *user_data = NULL;

	/* check for hotplug support */
	if (!usbi_backend_has_hotplug())
		return NULL;

	usbi_dbg("get hotplug cb %d user data", callback_handle);

	ctx = usbi_get_context(ctx);

	usbi_mutex_lock(&ctx->usb_devs_lock);
	for_each_hotplug_cb(ctx, hotplug_cb) {
		if (hotplug_cb->handle == callback_handle) {
			user_data = hotplug_cb->user_data;
			break;
		}
	}
	usbi_mutex_lock(&ctx->usb_devs_lock);

	return user_data;
}
