/*
 * kernel/include/device/platform.h
 * 
 * Platform device abstraction
 * Simplified interface for non-discoverable devices from device tree
 */

#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <device/device.h>
#include <device/resource.h>
#include <stdint.h>
#include <stdbool.h>

/* Platform device structure */
struct platform_device {
    struct device       dev;            /* Inherits from device */
    void                *platform_data; /* Platform-specific data */
    uint32_t            num_resources;  /* Total number of resources */
};

/* Platform driver structure */
struct platform_driver {
    struct driver       driver;         /* Inherits from driver */
    int (*probe)(struct platform_device *pdev);
    int (*remove)(struct platform_device *pdev);
    int (*suspend)(struct platform_device *pdev);
    int (*resume)(struct platform_device *pdev);
    const char          **compatible;  /* Array of compatible strings */
    uint32_t            num_compatible; /* Number of compatible strings */
};

/* Platform device API */

/* Device registration */
struct platform_device *platform_device_alloc(const char *name);
void platform_device_free(struct platform_device *pdev);
int platform_device_register(struct platform_device *pdev);
void platform_device_unregister(struct platform_device *pdev);

/* Simplified registration with resources */
struct platform_device *platform_device_register_simple(const char *name,
                                                      struct resource *res,
                                                      int num_res,
                                                      void *pdata);

/* Resource helpers */
struct resource *platform_get_resource(struct platform_device *pdev,
                                     resource_type_t type, int index);
struct resource *platform_get_resource_byname(struct platform_device *pdev,
                                            resource_type_t type,
                                            const char *name);
int platform_get_irq(struct platform_device *pdev, int index);
int platform_get_irq_byname(struct platform_device *pdev, const char *name);

/* Memory mapping helpers */
void __iomem *platform_ioremap_resource(struct platform_device *pdev, int index);
void platform_iounmap(void __iomem *addr);

/* Platform data access */
void platform_set_drvdata(struct platform_device *pdev, void *data);
void *platform_get_drvdata(struct platform_device *pdev);

/* Driver registration */
int platform_driver_register(struct platform_driver *pdrv);
void platform_driver_unregister(struct platform_driver *pdrv);

/* Device/driver matching */
bool platform_match_device(struct platform_device *pdev,
                          struct platform_driver *pdrv);

/* Conversion helpers */
static inline struct platform_device *to_platform_device(struct device *dev) {
    return (struct platform_device *)dev;
}

static inline struct device *platform_device_to_device(struct platform_device *pdev) {
    return &pdev->dev;
}

/* Common platform device types */
#define PLATFORM_DEVID_NONE     (-1)
#define PLATFORM_DEVID_AUTO     (-2)

/* Platform device macros */
#define platform_device_for_each_resource(pdev, res, type) \
    device_for_each_resource(res, &(pdev)->dev, type)

/* Debug helpers */
void platform_device_print_info(struct platform_device *pdev);

#endif /* __PLATFORM_H */