/*
 * RGB+W LED Driver header
 *
 * Copyright 2015 Tudor Design Systems, LLC.
 *
 * Author: Cody Tudor <cody.tudor@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __RGBW_H_INCLUDED
#define __RGBW_H_INCLUDED

#include <linux/kernel.h>
#include <linux/module.h>     
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kdev_t.h>
#include <linux/of_gpio.h>




/* Notes on locking:
 *
 * rgbw_device->ops_lock is an internal backlight lock protecting the
 * ops pointer and no code outside the core should need to touch it.
 *
 * Access to update_status() is serialised by the update_lock mutex since
 * most drivers seem to need this and historically get it wrong.
 *
 * Most drivers don't need locking on their get_brightness() method.
 * If yours does, you need to implement it in the driver. You can use the
 * update_lock mutex if appropriate.
 *
 * Any other use of the locks below is probably wrong.
 */

/* Types */

#define PULSE_VALUE_PER_NS 50000000
#define PULSE_VALUE_PER_MS (PULSE_VALUE_PER_NS / 1000000)
#define BLINK_STATE_PER_NS 750000000
#define BLINK_STATE_PER_MS (BLINK_STATE_PER_NS / 1000000)

enum rgbw_colors {
    COLOR_RED = 0,
    COLOR_GREEN = 1,
    COLOR_BLUE = 2,
    COLOR_WHITE = 3,
    MAX_COLORS = 4,
    INVALID_COLOR = 255,
};

enum rgbw_type {
    RGBW_PWM = 1,
    RGBW_GPIO,
    RGBW_TYPE_MAX,
    RGBW_TYPE_INVALID,
};

enum hrtimer_type {
    HRTIMER_PULSE = 0,
    HRTIMER_BLINK,
    HRTIMER_HEARTBEAT,
    HRTIMER_RAINBOW,
    MAX_HRTIMER,
};

struct rgbw_device;

struct rgbw_ops {
    unsigned int options;

#define RGBW_CORE_SUSPENDRESUME   (1 << 0)

    /* Notify the RGBW driver some property has changed */
    int (*update_status)(struct rgbw_device *);
};

struct rgbw_actions {
    /* holds a color for pulse function, one at a time */
    int pcolor;
    /* holds the current state of blink function */
    int bstate;
    /* previous state to restore after function stops */
    unsigned int rgbw_values[MAX_COLORS];
    unsigned int state;

#define RGBW_PULSE_ON           (1 << 0)
#define RGBW_BLINK_ON           (1 << 1)
#define RGBW_HB_ON              (1 << 2)
#define RGBW_RB_ON              (1 << 3)
};

/* This structure defines all the properties of a backlight */
struct rgbw_properties {
    /* Current User requested brightness (0 - max_brightness) */
    int brightness;
    /* Maximal value for brightness (read-only) */
    int max_brightness;
    /* Counter used by pulse function or other future needs */
    int cntr;
    /* RGBW color */
    enum rgbw_colors color;
    /* RGBW type */
    enum rgbw_type type;
    /* Flags used to signal drivers of state changes */
    /* Upper 4 bits are reserved for driver internal use */
    unsigned int state;

#define RGBW_CORE_SUSPENDED     (1 << 0)    /* rgbw is suspended */

};

struct rgbw_device {
    /* RGBW properties */
    struct rgbw_properties props[MAX_COLORS];
    /* hrtimer struct for our pwm actions */
    struct hrtimer rgbw_hrtimer[MAX_HRTIMER];
    
    struct rgbw_actions acts;
    
    /* Serialise access to update_status method */
    struct mutex update_lock;

    /* This protects the 'ops' field. If 'ops' is NULL, the driver that
       registered this device has been unloaded, and if class_get_devdata()
       points to something in the body of that driver, it is also invalid. */
    struct mutex ops_lock;
    const struct rgbw_ops *ops;

    struct device dev;

    int use_count;
};


/* Global public functions */

static inline void rgbw_update_status(struct rgbw_device *rgbw_dev)
{
    mutex_lock(&rgbw_dev->update_lock);
    if (rgbw_dev->ops && rgbw_dev->ops->update_status)
        rgbw_dev->ops->update_status(rgbw_dev);
    mutex_unlock(&rgbw_dev->update_lock);
}

extern const char *const color_names[];

extern struct rgbw_device *rgbw_device_register(const char *name,
    struct device *dev, void *devdata, const struct rgbw_ops *ops,
    struct rgbw_properties props[MAX_COLORS], struct rgbw_actions *acts);
extern void rgbw_device_unregister(struct rgbw_device *rgbw_dev);

#define to_rgbw_device(obj) container_of(obj, struct rgbw_device, dev)

static inline void * rgbw_get_data(struct rgbw_device *rgbw_dev)
{
    return dev_get_drvdata(&rgbw_dev->dev);
}


struct rgbw_device *of_find_rgbw_by_node(struct device_node *node);


#endif  /* __RGBW_H_INCLUDED */
