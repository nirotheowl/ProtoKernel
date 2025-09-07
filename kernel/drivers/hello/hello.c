#include <drivers/driver.h>
#include <printk.h>

static int hello_probe(struct device *dev)
{
    (void)dev;  // silence unused parameter warning
    printk("Hello driver probed!\n");
    return PROBE_SCORE_GENERIC;
}

static int hello_attach(struct device *dev)
{
    (void)dev;
    printk("Hello driver attached!\n");
    return 0;
}

static const struct driver_ops hello_ops = {
    .probe  = hello_probe,
    .attach = hello_attach,
};

static struct driver hello_driver = {
    .name       = "hello_driver",
    .class      = DRIVER_CLASS_MISC,
    .ops        = &hello_ops,
    .matches    = NULL,
    .num_matches= 0,
    .priority   = 0,
    .priv_size  = 0,
    .flags      = DRIVER_FLAG_BUILTIN
};

int hello_driver_init(void)
{
    return driver_register(&hello_driver);
}

