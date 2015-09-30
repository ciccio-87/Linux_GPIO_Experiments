#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/hrtimer.h>


static unsigned int gpio_led = 18;

static bool value;

static unsigned long period_ms = 200;
static unsigned long period_ns;
static ktime_t ktime_period_ns;
static struct hrtimer my_hrtimer;

static struct kobject *example_kobj;

module_param(period_ms, ulong, 0000);
MODULE_PARM_DESC(period_ms, "Blink duration");
module_param(gpio_led, uint, 0000);
MODULE_PARM_DESC(gpio_led, "led GPIO pin");


static enum hrtimer_restart led_blink_function(struct hrtimer *timer)
{
	unsigned long tjnow;
	ktime_t kt_now;
	int ret_overrun;

	value = !value;
	gpio_set_value(gpio_led, value);

	tjnow = jiffies;
	kt_now = hrtimer_cb_get_time(&my_hrtimer);
	ret_overrun = hrtimer_forward(&my_hrtimer, kt_now, ktime_period_ns);

	return HRTIMER_RESTART;
}


static int __init blink_init(void)
{

	int retval = 0;
	int dir_err;
	struct timespec tp_hr_res;

	if (!gpio_is_valid(gpio_led)) {
		pr_alert("The requested GPIO is not available\n");
		retval = -EINVAL;
		goto fail;
	}
	if (gpio_request(gpio_led, "led_gpio")) {
		pr_alert("Unable to request gpio %d", gpio_led);
		retval = -EINVAL;
		goto fail;
	}
	dir_err = gpio_direction_output(gpio_led, value);
	if (dir_err < 0) {
		pr_alert("Impossible to set output direction");
		retval = -EINVAL;
		goto fail2;
	}


	hrtimer_get_res(CLOCK_MONOTONIC, &tp_hr_res);


	hrtimer_init(&my_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	my_hrtimer.function = &led_blink_function;
	period_ns = period_ms*((unsigned long)1E6L);
	ktime_period_ns = ktime_set(0, period_ns);
	hrtimer_start(&my_hrtimer, ktime_period_ns, HRTIMER_MODE_REL);

	goto fail;
fail2:
	gpio_free(gpio_led);
fail:
	return retval;
}


static void __exit blink_exit(void)
{
	int ret_cancel = 0;

	kobject_put(example_kobj);
	while (hrtimer_callback_running(&my_hrtimer))
		ret_cancel++;

	if (ret_cancel != 0)
		pr_info(
		"Waited for hrtimer callback to finish (%d)\n", ret_cancel);

	if (hrtimer_active(&my_hrtimer) != 0)
		ret_cancel = hrtimer_cancel(&my_hrtimer);
	if (hrtimer_is_queued(&my_hrtimer) != 0)
		ret_cancel = hrtimer_cancel(&my_hrtimer);

	gpio_free(gpio_led);
}



module_init(blink_init);
module_exit(blink_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple timer based led blinker for GPIO powered devices");
MODULE_AUTHOR("Samuele Baisi");
