#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>


static unsigned int gpio_in = 22;
static unsigned int gpio_led = 18;
static bool value;
static int dir_err;

static int irq_line;
static int irq_req_res;
static struct workqueue_struct *wq;
static struct work_struct *work;

static unsigned long period_ms;
static unsigned long period_ns;
static ktime_t ktime_period_ns;
static struct hrtimer my_hrtimer;

static int periods[] = {0, 1000, 500, 200, 100, 1};
static int index;

module_param(gpio_in, uint, 0000);
MODULE_PARM_DESC(gpio_in, "button (interrupt) GPIO pin");
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


/*
 * Interrupt Handler
 */
irqreturn_t change_state_interrupt(int irq,
	void *dev_id, struct pt_regs *regs)
{
	queue_work(wq, work);
	return IRQ_HANDLED;
}


static void manage_timer(struct work_struct *work)
{
	struct timespec tp_hr_res;

	index = (index + 1) % 6;
	pr_info("ControlBlink: passing to %i period\n", periods[index]);
	if (index > 1)
		hrtimer_cancel(&my_hrtimer);
	if (index == 0 || index == 5) {
		gpio_set_value(gpio_led, periods[index]);
		goto exit;
	}

	period_ms = periods[index];
	hrtimer_get_res(CLOCK_MONOTONIC, &tp_hr_res);

	hrtimer_init(&my_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	my_hrtimer.function = &led_blink_function;
	period_ns = period_ms*((unsigned long)1E6L);
	ktime_period_ns = ktime_set(0, period_ns);
	hrtimer_start(&my_hrtimer, ktime_period_ns, HRTIMER_MODE_REL);

exit:
	return;
}


static int __init blink_init(void)
{

	int retval = 0;

	if (!gpio_is_valid(gpio_led)) {
		pr_alert("The requested GPIO is not available\n");
		retval = -EINVAL;
		goto fail;
	}
	if (!gpio_is_valid(gpio_in)) {
		pr_alert("The requested GPIO is not available\n");
		retval = -EINVAL;
		goto fail;
	}
	if (gpio_request(gpio_led, "led_gpio")) {
		pr_alert("Unable to request gpio %d", gpio_led);
		retval = -EINVAL;
		goto fail;
	}
	if (gpio_request(gpio_in, "gpio_in")) {
		pr_alert("Unable to request gpio %d", gpio_in);
		retval = -EINVAL;
		goto fail2;
	}
	dir_err = gpio_direction_output(gpio_led, value);
	if (dir_err < 0) {
		pr_alert("Impossible to set output direction");
		retval = -EINVAL;
		goto fail3;
	}
	gpio_export(gpio_in, false);
	dir_err = gpio_direction_input(gpio_in);
	if (dir_err < 0) {
		pr_alert("Impossible to set input direction");
		retval = -EINVAL;
		goto fail3;
	}
	gpio_set_debounce(gpio_in, 200);
	gpio_export(gpio_in, false);

		wq = create_singlethread_workqueue("blink_wq");
	if (!wq) {
		pr_alert("unable to setup workqueue\n");
		retval = -EINVAL;
		goto fail3;
	}

	work = (struct work_struct *)
		kmalloc(sizeof(struct work_struct), GFP_KERNEL);
	if (!work)
		goto fail3;
	INIT_WORK(work, manage_timer);

	irq_line = gpio_to_irq(gpio_in);
	if (irq_line < 0) {
		pr_alert("Gpio %d cannot be used as interrupt", gpio_in);
		retval = -EINVAL;
		goto destroy_workqueue;
	}

	irq_req_res = request_irq(irq_line,
	(irq_handler_t)change_state_interrupt, IRQF_TRIGGER_RISING,
	"gpio_change_state", NULL);

	if (irq_req_res < 0) {
		if (irq_req_res == -EBUSY)
			retval = irq_req_res;
		else
			retval = -EINVAL;
		goto destroy_workqueue;
	}

	goto fail;

destroy_workqueue:
	flush_workqueue(wq);
	destroy_workqueue(wq);
	kfree(work);
fail3:
	gpio_free(gpio_in);
fail2:
	gpio_free(gpio_led);
fail:
	return retval;
}


static void __exit blink_exit(void)
{
	int ret_cancel = 0;

	while (hrtimer_callback_running(&my_hrtimer))
		ret_cancel++;

	if (ret_cancel != 0)
		pr_info(
		"Waited for hrtimer callback to finish (%d)\n", ret_cancel);

	if (hrtimer_active(&my_hrtimer) != 0)
		ret_cancel = hrtimer_cancel(&my_hrtimer);

	if (hrtimer_is_queued(&my_hrtimer) != 0)
		ret_cancel = hrtimer_cancel(&my_hrtimer);

	free_irq(irq_line, NULL);
	flush_workqueue(wq);
	destroy_workqueue(wq);
	kfree(work);
	gpio_free(gpio_in);
	gpio_free(gpio_led);
}



module_init(blink_init);
module_exit(blink_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Putting all together, button driven led flasher");
MODULE_AUTHOR("Samuele Baisi");
