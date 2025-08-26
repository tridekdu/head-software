// SPDX-License-Identifier: MIT
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/preempt.h>

#define MAX_USER_SIZE  (1024)              // bytes per write
// Pi Zero 2 W (BCM2837/2710) peripheral base 0x3F000000, GPIO at +0x200000
#define BCM283X_GPIO_BASE  0x3F200000

#define PIN_DAT   17                       // 1-wire data to LEDs

// GPIO register offsets
#define GPFSEL0   0x00
#define GPSET0    0x1C
#define GPCLR0    0x28

// WS2812/SK6805 timing (ns). Tperiod ≈ 1250 ns @ 800 kHz
#define T0H_NS    350
#define T0L_NS    800
#define T1H_NS    700
#define T1L_NS    600
// Latch/reset gap. 80 µs is typical; 300 µs is conservative.
#define TRES_US   300

static struct proc_dir_entry *proc_entry;
static void __iomem *gpio_base;

// helpers
static inline void gpio_fsel_output(unsigned int pin)
{
    void __iomem *fsel = gpio_base + GPFSEL0 + (pin/10)*4;
    unsigned int shift = (pin % 10)*3;
    u32 v = readl_relaxed(fsel);
    v &= ~(7u << shift);
    v |=  (1u << shift);       // 001 = output
    writel_relaxed(v, fsel);
    // small barrier to ensure pad mode applied before toggling
    mb();
}

static inline void gpio_set(unsigned int pin)
{
    writel_relaxed(1u << pin, gpio_base + GPSET0);
}

static inline void gpio_clr(unsigned int pin)
{
    writel_relaxed(1u << pin, gpio_base + GPCLR0);
}

static inline void ws2812_write_bit(int bit)
{
    // High first, then low; widths select 0 or 1
    gpio_set(PIN_DAT);
    if (bit) ndelay(T1H_NS); else ndelay(T0H_NS);
    gpio_clr(PIN_DAT);
    if (bit) ndelay(T1L_NS); else ndelay(T0L_NS);
}

// === /proc interface ===
static ssize_t driver_read(struct file *file, char __user *user, size_t size, loff_t *off)
{
    static const char msg[] = "WS2812 proc. Write GRB bytes.\n";
    if (*off >= sizeof(msg)) return 0;
    if (size > sizeof(msg)) size = sizeof(msg);
    if (copy_to_user(user, msg, size)) return -EFAULT;
    *off += size;
    return size;
}

static ssize_t driver_write(struct file *file, const char __user *user, size_t size, loff_t *off)
{
    u8 *kbuf;
    size_t i, len = min_t(size_t, size, MAX_USER_SIZE);
    unsigned long flags;

    if (len == 0) return 0;

    kbuf = kmalloc(len, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;
    if (copy_from_user(kbuf, user, len)) { kfree(kbuf); return -EFAULT; }

    // Tight timing critical section
    local_irq_save(flags);
    preempt_disable();

    for (i = 0; i < len; i++) {
        u8 b = kbuf[i];
        // WS2812 expects MSB first
        ws2812_write_bit((b & 0x80) != 0);
        ws2812_write_bit((b & 0x40) != 0);
        ws2812_write_bit((b & 0x20) != 0);
        ws2812_write_bit((b & 0x10) != 0);
        ws2812_write_bit((b & 0x08) != 0);
        ws2812_write_bit((b & 0x04) != 0);
        ws2812_write_bit((b & 0x02) != 0);
        ws2812_write_bit((b & 0x01) != 0);
    }

    preempt_enable();
    local_irq_restore(flags);

    kfree(kbuf);

    // Latch. Non-critical; allow interrupts.
    udelay(TRES_US);

    return size;
}

static const struct proc_ops eye_proc_fops = {
    .proc_read  = driver_read,
    .proc_write = driver_write,
};

static int __init panel_driver_init(void)
{
    pr_info("ledpanels: init\n");

    gpio_base = ioremap(BCM283X_GPIO_BASE, PAGE_SIZE);
    if (!gpio_base) {
        pr_err("ledpanels: ioremap failed\n");
        return -ENOMEM;
    }

    proc_entry = proc_create("ledpanels", 0666, NULL, &eye_proc_fops);
    if (!proc_entry) {
        pr_err("ledpanels: proc create failed\n");
        iounmap(gpio_base);
        return -ENOMEM;
    }

    gpio_fsel_output(PIN_DAT);
    gpio_clr(PIN_DAT);

    pr_info("ledpanels: ready (/proc/ledpanels)\n");
    return 0;
}

static void __exit panel_driver_exit(void)
{
    proc_remove(proc_entry);
    iounmap(gpio_base);
    pr_info("ledpanels: unloaded\n");
}

module_init(panel_driver_init);
module_exit(panel_driver_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("32");
MODULE_DESCRIPTION("WS2812/SK6805 1-wire bitbang via procfs");
MODULE_VERSION("1");
