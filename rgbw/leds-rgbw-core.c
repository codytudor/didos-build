/*
 * RGB+W LED Class Core
 *
 * Copyleft 2016 Tudor Design Systems, LLC.
 *
 * Author: Cody Tudor <cody.tudor@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * This driver is a re-implementation of the generic backlight class
 * device in ./drivers/video/backlight/backlight.c and pwm_bl.c; functions 
 * and general flow were used as a skeleton for the management and 
 * control operations. 
 * 
 * This driver requires a device tree entry with the compatible
 * string "pwm-rgbw" The driver will create a class device in /sys/class
 * that will provide the same functionality as a the backlight class
 * device. The DT entry must contain a PWM for red, grn, & blue channels in 
 * addition to a GPIO for the wht channel. Default brightness for each
 * is optional but brightness levels MUST be defined.  
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "rgbw.h"
#include <linux/string.h>
#include <linux/delay.h>

static const char *const rgbw_types[] = {
    [RGBW_PWM] = "hard_pwm",
    [RGBW_GPIO] = "soft_pwm",
};

char *bin2hex(char *dst, const void *src, size_t count)
{
          const unsigned char *_src = src;
  
          while (count--)
                  dst = hex_byte_pack(dst, *_src++);
          return dst;
}

static void rgbw_generate_event(struct rgbw_device *rgbw_dev)
{
    char *envp[2];

    envp[0] = "SOURCE=sysfs";
    envp[1] = NULL;
    kobject_uevent_env(&rgbw_dev->dev.kobj, KOBJ_CHANGE, envp);
    sysfs_notify(&rgbw_dev->dev.kobj, NULL, "rgbw_values");
}

static ssize_t rgbw_set_rainbow(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    int cntr;
    unsigned long cmd;
    
    rc = kstrtoul(buf, 0, &cmd);
    if (rc)
        return rc;
    
    if ((cmd < 0) || (cmd > 1))
        return -EINVAL;
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
        pr_info("pulse is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
        pr_info("blink is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_HB_ON) {
        pr_info("heartbeat is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_RB_ON) {
        if (cmd == 0) {
            rgbw_dev->acts.state &= ~RGBW_RB_ON;
            /* Wait for the last timer to expire */
            msleep(BLINK_STATE_PER_MS);    
        }
        else {
            pr_info("rainbow is currently active, stop it first...\n");
            return count;
        }
    }
       
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops) { 
        if (!cmd) {
            if (rgbw_dev->acts.bstate < INVALID_COLOR) {
                /*  restore our previous state before starting pulse */
                for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                    rgbw_dev->props[cntr].brightness = rgbw_dev->acts.rgbw_values[cntr];
                }
                rgbw_dev->acts.bstate = INVALID_COLOR;                              
            }
        }   
        else {
                rgbw_dev->acts.bstate = INVALID_COLOR;
                rgbw_dev->acts.state |= RGBW_RB_ON;
        }
            
        if (rgbw_dev->acts.state & RGBW_RB_ON) {
            for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                rgbw_dev->acts.rgbw_values[cntr] = rgbw_dev->props[cntr].brightness;
            }
        }
        
        rgbw_update_status(rgbw_dev); 
        msleep(BLINK_STATE_PER_MS / 4);
        rgbw_update_status(rgbw_dev);
        rc = count;    
    }
    else {
        rc = -ENXIO;
    }
    mutex_unlock(&rgbw_dev->ops_lock);
    
    if (rgbw_dev->acts.state & RGBW_RB_ON)
        hrtimer_start(&rgbw_dev->rgbw_hrtimer[HRTIMER_RAINBOW], ktime_set(0,1000), HRTIMER_MODE_REL);

    rgbw_generate_event(rgbw_dev);
       
    return rc;
}

static ssize_t rgbw_set_heartbeat(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    int cntr;
    unsigned long cmd;
    
    rc = kstrtoul(buf, 0, &cmd);
    if (rc)
        return rc;
    
    if ((cmd < 0) || (cmd > 1))
        return -EINVAL;
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
        pr_info("pulse is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
        pr_info("blink is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_HB_ON) {
        if (cmd == 0) {
            rgbw_dev->acts.state &= ~RGBW_HB_ON;
            /* Wait for the last timer to expire */
            msleep(BLINK_STATE_PER_MS);
        }
        else {
            pr_info("heartbeat is currently active, stop it first...\n");
            return count;   
        }
    }
    if (rgbw_dev->acts.state & RGBW_RB_ON) {
        pr_info("rainbow is currently active, stop it first...\n");
        return count;
    }
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops) { 
        if (!cmd) {
            if (rgbw_dev->acts.bstate <= MAX_COLORS) {                
                /*  restore our previous state before starting pulse */
                for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                    rgbw_dev->props[cntr].brightness = rgbw_dev->acts.rgbw_values[cntr];
                } 
                rgbw_dev->acts.bstate = INVALID_COLOR;             
            }
        }   
        else {  
                rgbw_dev->acts.bstate = 0;
                rgbw_dev->acts.state |= RGBW_HB_ON;
        }
            
        if (rgbw_dev->acts.state & RGBW_HB_ON) {
            for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                rgbw_dev->acts.rgbw_values[cntr] = rgbw_dev->props[cntr].brightness;
            }
        }
       
        rgbw_update_status(rgbw_dev); 
        rc = count;    
    }
    else {
        rc = -ENXIO;
    }
    mutex_unlock(&rgbw_dev->ops_lock);
    
    if (rgbw_dev->acts.state & RGBW_HB_ON)
        hrtimer_start(&rgbw_dev->rgbw_hrtimer[HRTIMER_HEARTBEAT], ktime_set(0,1000), HRTIMER_MODE_REL);

    rgbw_generate_event(rgbw_dev);
       
    return rc;
}

static ssize_t rgbw_set_blink(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    int cntr;
    unsigned long cmd;
    
    rc = kstrtoul(buf, 0, &cmd);
    if (rc)
        return rc;
    
    if ((cmd < 0) || (cmd > 1))
        return -EINVAL;
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
        pr_info("pulse is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
        if (cmd == 0) {
            rgbw_dev->acts.state &= ~RGBW_BLINK_ON;
            /* Wait for the last timer to expire */
            msleep(BLINK_STATE_PER_MS + 5);
        }
        else {
            pr_info("blink is currently active, stop it first...\n");
            return count;
        }
    }
    if (rgbw_dev->acts.state & RGBW_HB_ON) {
        pr_info("heartbeat is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_RB_ON) {
        pr_info("rainbow is currently active, stop it first...\n");
        return count;
    }
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops) { 
        if (!cmd) {
            if (rgbw_dev->acts.bstate <= MAX_COLORS) {
                /*  restore our previous state before starting pulse */
                for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                    rgbw_dev->props[cntr].brightness = rgbw_dev->acts.rgbw_values[cntr];
                } 
                rgbw_dev->acts.bstate = INVALID_COLOR;             
            }
        }   
        else {
                rgbw_dev->acts.bstate = 0;
                rgbw_dev->acts.state |= RGBW_BLINK_ON;
        }
            
        if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
            for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                rgbw_dev->acts.rgbw_values[cntr] = rgbw_dev->props[cntr].brightness;
            }
        }
       
        rgbw_update_status(rgbw_dev); 
        rc = count;    
    }
    else {
        rc = -ENXIO;
    }
    mutex_unlock(&rgbw_dev->ops_lock);
    
    if (rgbw_dev->acts.state & RGBW_BLINK_ON)
        hrtimer_start(&rgbw_dev->rgbw_hrtimer[HRTIMER_BLINK], ktime_set(0,1000), HRTIMER_MODE_REL);
        
    rgbw_generate_event(rgbw_dev);
       
    return rc;
}

static ssize_t rgbw_set_pulse(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    int cntr;
    
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
        if (strncmp(buf, "stop", strlen("stop")) == 0) {
            rgbw_dev->acts.state &= ~RGBW_PULSE_ON;
            /* Wait for the last timer to expire */
            msleep(BLINK_STATE_PER_MS);
        }
        else {
            pr_info("pulse is currently active, stop it first...\n");
            return count;
        }
    }
    if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
        pr_info("blink is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_HB_ON) {
        pr_info("heartbeat is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_RB_ON) {
        pr_info("rainbow is currently active, stop it first...\n");
        return count;
    }
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops) { 
        if (strncmp(buf, "stop", strlen("stop")) == 0) {
            if (rgbw_dev->acts.pcolor < MAX_COLORS) {
                /*  restore our previous state before starting pulse */
                for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                    rgbw_dev->props[cntr].brightness = rgbw_dev->acts.rgbw_values[cntr];
                    rgbw_dev->props[cntr].cntr = 0;
                } 
                rgbw_dev->acts.pcolor = INVALID_COLOR;            
            }
        }   
        else if (strncmp(buf, "red", strlen("red")) == 0) {
            rgbw_dev->acts.pcolor = COLOR_RED;
            rgbw_dev->acts.state |= RGBW_PULSE_ON;
        }
        else if (strncmp(buf, "green", strlen("green")) == 0) {
            rgbw_dev->acts.pcolor = COLOR_GREEN;
            rgbw_dev->acts.state |= RGBW_PULSE_ON;
        }
        else if (strncmp(buf, "blue", strlen("blue")) == 0) {
            rgbw_dev->acts.pcolor = COLOR_BLUE;
            rgbw_dev->acts.state |= RGBW_PULSE_ON;
        }
        else if (strncmp(buf, "white", strlen("white")) == 0) {
            rgbw_dev->acts.pcolor = COLOR_WHITE;
            rgbw_dev->acts.state |= RGBW_PULSE_ON;
        }
        else {
            pr_info("pulse only takes the arguments: [red | green | blue | white | stop]\n");
            mutex_unlock(&rgbw_dev->ops_lock);
            return count;
        }
            
        if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
            for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
                rgbw_dev->acts.rgbw_values[cntr] = rgbw_dev->props[cntr].brightness;
                rgbw_dev->props[cntr].brightness = 0;
            }
            /* start the color's counter at 0 */
            rgbw_dev->props[rgbw_dev->acts.pcolor].cntr = 0;
        }
        
        rgbw_update_status(rgbw_dev);
        rc = count;    
    }
    else {
        rc = -ENXIO;
    }
    mutex_unlock(&rgbw_dev->ops_lock);
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON)
        hrtimer_start(&rgbw_dev->rgbw_hrtimer[HRTIMER_PULSE], ktime_set(0,1000), HRTIMER_MODE_REL);
        
    rgbw_generate_event(rgbw_dev);
       
    return rc;
}

static ssize_t rgbw_show_values(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    char html_value[strlen("#RRGGBBWW")];
    char tmp[3] = "";
    int cntr;
    
    html_value[0] = '#';
    html_value[1] = '\0';
    
    for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
        bin2hex(tmp, &rgbw_dev->props[cntr].brightness, 1); 
        tmp[2] = '\0';
        strncat(html_value, tmp, strlen(tmp)); 
    }
    
    return sprintf(buf, "HTML Code (#RRGGBBWW) = %s\nRed = %d\nGreen = %d\nBlue = %d\nWhite = %d\n",
            html_value,
            rgbw_dev->props[COLOR_RED].brightness,
            rgbw_dev->props[COLOR_GREEN].brightness,
            rgbw_dev->props[COLOR_BLUE].brightness,
            rgbw_dev->props[COLOR_WHITE].brightness);
}

static ssize_t rgbw_show_single_color(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);

    int color = COLOR_RED;
    
    if (strcmp(attr->attr.name, "red_value") == 0) {
        color = COLOR_RED;
    }
    else if (strcmp(attr->attr.name, "green_value") == 0) {
        color = COLOR_GREEN;
    }
    else if (strcmp(attr->attr.name, "blue_value") == 0) {
        color = COLOR_BLUE;
    }
    else if (strcmp(attr->attr.name, "white_value") == 0) {
        color = COLOR_WHITE;
    }
    else {
        pr_info("this is not a valid function, it is %s\n", attr->attr.name);
        return -ENXIO;
    }
    
    return sprintf(buf, "%d\n", rgbw_dev->props[color].brightness);
}

static ssize_t rgbw_store_single_color(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    unsigned long brightness;
    int color = COLOR_RED;
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
        pr_info("pulse is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
        pr_info("blink is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_HB_ON) {
        pr_info("heartbeat is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_RB_ON) {
        pr_info("rainbow is currently active, stop it first...\n");
        return count;
    }
    
    rc = kstrtoul(buf, 0, &brightness);
        if (rc)
            return rc;
    
    if (strcmp(attr->attr.name, "red_value") == 0) {
        color = COLOR_RED;
    }
    else if (strcmp(attr->attr.name, "green_value") == 0) {
        color = COLOR_GREEN;
    }
    else if (strcmp(attr->attr.name, "blue_value") == 0) {
        color = COLOR_BLUE;
    }
    else if (strcmp(attr->attr.name, "white_value") == 0) {
        color = COLOR_WHITE;
    }
    else {
        pr_info("this is not a valid function, it is %s\n", attr->attr.name);
        rc = -ENXIO;
    }
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops) {
        if (brightness > rgbw_dev->props[color].max_brightness)
            rc = -EINVAL;
        else {
            pr_debug("set brightness to %lu\n", brightness);
            rgbw_dev->props[color].brightness = brightness;
            rgbw_update_status(rgbw_dev);
            rc = count;
        }
    }
    else {
        rc = -ENXIO;
    }
    mutex_unlock(&rgbw_dev->ops_lock);

    rgbw_generate_event(rgbw_dev);
       
    return rc;
}

static ssize_t rgbw_store_values(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc = 0, cntr, hi, lo;
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    unsigned char brightness[MAX_COLORS];
    char red_hex[3] = ""; 
    char grn_hex[3] = ""; 
    char blu_hex[3] = "";
    char wht_hex[3] = "";
    
    if (rgbw_dev->acts.state & RGBW_PULSE_ON) {
        pr_info("pulse is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_BLINK_ON) {
        pr_info("blink is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_HB_ON) {
        pr_info("heartbeat is currently active, stop it first...\n");
        return count;
    }
    if (rgbw_dev->acts.state & RGBW_RB_ON) {
        pr_info("rainbow is currently active, stop it first...\n");
        return count;
    }

    /* Change the buf string into a valid RGB[W] value
     * and recursively change the brightness of each color to match
     */
    if (strncmp(buf, "#", 1) != 0) {
        dev_err(dev, "your HTML RGB[W] value must begin with the \"#\" symbol\n");
        return -EINVAL;
    }

    if ((strlen(buf) - 1) < 7) {
        dev_err(dev, "your HTML RGB[W] value is too short with %d characters. Use \"#RRGGBB[WW]\" format\n", strlen(buf) - 1);
        return -EINVAL;
    }
    
    if ((strlen(buf) - 1) > 9) {
        dev_err(dev, "your HTML RGB[W] value is too long with %d characters. Use \"#RRGGBB[WW]\" format\n", strlen(buf) - 1);
        return -EINVAL;
    }
    
    if ((strlen(buf) - 1) == 8) {
        dev_err(dev, "your HTML [W] value is incomplete. Use \"#RRGGBB[WW]\" format\n");
        return -EINVAL;
    }
    
    
    
    strncpy(red_hex, buf+1, 2);
    red_hex[2] = '\0'; 
    strncpy(grn_hex, buf+3, 2);
    grn_hex[2] = '\0'; 
    strncpy(blu_hex, buf+5, 2);
    blu_hex[2] = '\0'; 
    if ((strlen(buf) - 1) == 9) {
        strncpy(wht_hex, buf+7, 2);
        wht_hex[2] = '\0'; 
    }
          
    hi = hex_to_bin(red_hex[0]);
    lo = hex_to_bin(red_hex[1]);

    if ((hi < 0) || (lo < 0)) {
        dev_err(dev, "your HTML red value is not in hex format, %u is not a valid conversion\n", (hi << 4) | lo);
        return -EINVAL;
    }
    
    brightness[COLOR_RED] = (hi << 4) | lo;
    
    hi = hex_to_bin(grn_hex[0]);
    lo = hex_to_bin(grn_hex[1]);

    if ((hi < 0) || (lo < 0)) {
        dev_err(dev, "your HTML green value is not in hex format, %u is not a valid conversion\n", (hi << 4) | lo);
        return -EINVAL;
    }
    
    brightness[COLOR_GREEN] = (hi << 4) | lo;
    
    hi = hex_to_bin(blu_hex[0]);
    lo = hex_to_bin(blu_hex[1]);

    if ((hi < 0) || (lo < 0)) {
        dev_err(dev, "your HTML blue value is not in hex format, %u is not a valid conversion\n", (hi << 4) | lo);
        return -EINVAL;
    }
    
    brightness[COLOR_BLUE] = (hi << 4) | lo;
   
   
    if (strlen(wht_hex) > 1) {
        hi = hex_to_bin(wht_hex[0]);
        lo = hex_to_bin(wht_hex[1]);

        if ((hi < 0) || (lo < 0)) {
            dev_err(dev, "your HTML white value is not in hex format, %u is not a valid conversion\n", (hi << 4) | lo);
            return -EINVAL;
        }
    
        brightness[COLOR_WHITE] = (hi << 4) | lo;
    }
    else
        brightness[COLOR_WHITE] = rgbw_dev->props[COLOR_WHITE].brightness;
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops) {
        for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
            pr_debug("set %s brightness to %d\n", color_names[cntr], brightness[cntr]);
            rgbw_dev->props[cntr].brightness = brightness[cntr];
        }
        rgbw_update_status(rgbw_dev);
        rc = count;
    }
    mutex_unlock(&rgbw_dev->ops_lock);

    rgbw_generate_event(rgbw_dev);

    return rc;
}

static ssize_t rgbw_show_types(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    char *all_types = "";
    
    sprintf(all_types, "Red = %s\nGreen = %s\nBlue = %s\nWhite = %s", 
            rgbw_types[rgbw_dev->props[COLOR_RED].type], 
            rgbw_types[rgbw_dev->props[COLOR_GREEN].type], 
            rgbw_types[rgbw_dev->props[COLOR_BLUE].type], 
            rgbw_types[rgbw_dev->props[COLOR_WHITE].type]);
    
    return sprintf(buf, "%s\n", all_types);
}

static ssize_t rgbw_show_max_brightness(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    char *all_max = "";
    
    sprintf(all_max, "Red = %d\nGreen = %d\nBlue = %d\nWhite = %d", 
            rgbw_dev->props[COLOR_RED].max_brightness,
            rgbw_dev->props[COLOR_GREEN].max_brightness,
            rgbw_dev->props[COLOR_BLUE].max_brightness,
            rgbw_dev->props[COLOR_WHITE].max_brightness);
    
    return sprintf(buf, "%s\n", all_max);
}

static struct class *rgbw_class;

static int rgbw_suspend(struct device *dev, pm_message_t state)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    int cntr;
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops && rgbw_dev->ops->options & RGBW_CORE_SUSPENDRESUME) {
        for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
            rgbw_dev->props[cntr].state |= RGBW_CORE_SUSPENDED;
        }
        rgbw_update_status(rgbw_dev);
    }
    mutex_unlock(&rgbw_dev->ops_lock);

    return 0;
}

static int rgbw_resume(struct device *dev)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    int cntr;
    
    mutex_lock(&rgbw_dev->ops_lock);
    if (rgbw_dev->ops && rgbw_dev->ops->options & RGBW_CORE_SUSPENDRESUME) {
        for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
            rgbw_dev->props[cntr].state &= ~RGBW_CORE_SUSPENDED;
        }
        rgbw_update_status(rgbw_dev);
    }
    mutex_unlock(&rgbw_dev->ops_lock);

    return 0;
}

static void rgbw_device_release(struct device *dev)
{
    struct rgbw_device *rgbw_dev = to_rgbw_device(dev);
    kfree(rgbw_dev);
}

static DEVICE_ATTR(RGBW_values, 0664, rgbw_show_values, rgbw_store_values);
static DEVICE_ATTR(red_value, 0664, rgbw_show_single_color, rgbw_store_single_color);
static DEVICE_ATTR(green_value, 0664, rgbw_show_single_color, rgbw_store_single_color);
static DEVICE_ATTR(blue_value, 0664, rgbw_show_single_color, rgbw_store_single_color);
static DEVICE_ATTR(white_value, 0664, rgbw_show_single_color, rgbw_store_single_color);
static DEVICE_ATTR(per_color_max_value, 0444, rgbw_show_max_brightness, NULL);
static DEVICE_ATTR(RGBW_types, 0444, rgbw_show_types, NULL);
static DEVICE_ATTR(pulse, 0222, NULL, rgbw_set_pulse);
static DEVICE_ATTR(blink, 0222, NULL, rgbw_set_blink);
static DEVICE_ATTR(heartbeat, 0222, NULL, rgbw_set_heartbeat);
static DEVICE_ATTR(rainbow, 0222, NULL, rgbw_set_rainbow);

static struct attribute *rgbw_attrs[] = {
    &dev_attr_RGBW_values.attr,
    &dev_attr_red_value.attr,
    &dev_attr_green_value.attr,
    &dev_attr_blue_value.attr,
    &dev_attr_white_value.attr,
    &dev_attr_per_color_max_value.attr,
    &dev_attr_RGBW_types.attr,
    &dev_attr_pulse.attr,
    &dev_attr_blink.attr,
    &dev_attr_heartbeat.attr,
    &dev_attr_rainbow.attr,
    NULL,
};
ATTRIBUTE_GROUPS(rgbw);

/**
 * rgbw_device_register - create and register a new object of
 *   rgbw_device class.
 * @name: the name of the new object(must be the same as the name of the
 *   respective framebuffer device).
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use. The
 *   methods may retrieve it by using rgbw_get_data(rgbw_dev).
 * @ops: the color operations structure.
 *
 * Creates and registers new rgbw device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct rgbw_device *rgbw_device_register(const char *name,
    struct device *parent, void *devdata, const struct rgbw_ops *ops,
    struct rgbw_properties props[MAX_COLORS], struct rgbw_actions *acts)
{
    
    struct rgbw_device *new_rgbw_dev;
    int rc;
    int cntr;
    
    pr_debug("rgbw_device_register: name=%s\n", name);

    new_rgbw_dev = kzalloc(sizeof(struct rgbw_device), GFP_KERNEL);
    if (!new_rgbw_dev)
        return ERR_PTR(-ENOMEM);

    mutex_init(&new_rgbw_dev->update_lock);
    mutex_init(&new_rgbw_dev->ops_lock);

    new_rgbw_dev->dev.class = rgbw_class;
    new_rgbw_dev->dev.parent = parent;
    new_rgbw_dev->dev.release = rgbw_device_release;
    dev_set_name(&new_rgbw_dev->dev, name);
    dev_set_drvdata(&new_rgbw_dev->dev, devdata);

    /* Set default properties */
    if (props) {
        memcpy(&new_rgbw_dev->props, props,
               sizeof(struct rgbw_properties) * MAX_COLORS);
        for (cntr = COLOR_RED; cntr < MAX_COLORS; cntr++) {
            if (props[cntr].type <= 0 || props[cntr].type >= RGBW_TYPE_MAX) {
                WARN(1, "%s: invalid rgbw type", name);
            }
        }  
    } 

    rc = device_register(&new_rgbw_dev->dev);
    if (rc) {
        kfree(new_rgbw_dev);
        return ERR_PTR(rc);
    }

    new_rgbw_dev->ops = ops;
    new_rgbw_dev->acts = *acts;

    return new_rgbw_dev;
}
EXPORT_SYMBOL(rgbw_device_register);

/**
 * rgbw_device_unregister - unregisters a rgbw device object.
 * @rgbw_dev: the rgbw device object to be unregistered and freed.
 *
 * Unregisters a previously registered via rgbw_device_register object.
 */
void rgbw_device_unregister(struct rgbw_device *rgbw_dev)
{
    if (!rgbw_dev)
        return;

    mutex_lock(&rgbw_dev->ops_lock);
    rgbw_dev->ops = NULL;
    mutex_unlock(&rgbw_dev->ops_lock);

    device_unregister(&rgbw_dev->dev);
}
EXPORT_SYMBOL(rgbw_device_unregister);

static int of_parent_match(struct device *dev, const void *data)
{
    return dev->parent && dev->parent->of_node == data;
}

/**
 * of_find_rgbw_by_node() - find rgbw device by device-tree node
 * @node: device-tree node of the rgbw device
 *
 * Returns a pointer to the rgbw device corresponding to the given DT
 * node or NULL if no such rgbw device exists or if the device hasn't
 * been probed yet.
 *
 * This function obtains a reference on the rgbw device and it is the
 * caller's responsibility to drop the reference by calling put_device() on
 * the rgbw device's .dev field.  
 */
struct rgbw_device *of_find_rgbw_by_node(struct device_node *node)
{
    struct device *dev;

    dev = class_find_device(rgbw_class, NULL, node, of_parent_match);

    return dev ? to_rgbw_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_rgbw_by_node);


static void __exit rgbw_class_exit(void)
{
    class_destroy(rgbw_class);
}

static int __init rgbw_class_init(void)
{
  
    rgbw_class = class_create(THIS_MODULE, "rgbw");
    if (IS_ERR(rgbw_class)) {
        pr_warn("Unable to create rgbw class; errno = %ld\n",
            PTR_ERR(rgbw_class));
        return PTR_ERR(rgbw_class);
    }

    rgbw_class->dev_groups = rgbw_groups;
    rgbw_class->suspend = rgbw_suspend;
    rgbw_class->resume = rgbw_resume;
       
    return 0;
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register a light bar
 */
postcore_initcall(rgbw_class_init);
module_exit(rgbw_class_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cody Tudor <cody.tudor@gmail.com>");
MODULE_DESCRIPTION("RGB+W LLS Control Abstraction");
MODULE_ALIAS("platform:pwm-rgbw");
