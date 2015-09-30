#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>



static unsigned int gpio_in = 22;
static unsigned int gpio_led = 18;
static bool value;

static int irq_line;
static int irq_req_res;
static struct workqueue_struct *wq;
static struct work_struct *work;

module_param(gpio_in, uint, 0000);
MODULE_PARM_DESC(gpio_in, "button (interrupt) GPIO pin");
module_param(gpio_led, uint, 0000);
MODULE_PARM_DESC(gpio_led, "led GPIO pin");


static void change_led_state(struct work_struct *work)
{

	value = !value;
	gpio_set_value(gpio_led, value);

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


static int __init gpio_init(void)
{

	int dir_err = 0;
	int retval = 0;

	if (!gpio_is_valid(gpio_led)) {
		pr_alert("The requested GPIO is not available\n");
		retval = -EINVAL;
		goto invalid;
	}
	if (!gpio_is_valid(gpio_in)) {
		pr_alert("The requested GPIO is not available\n");
		retval = -EINVAL;
		goto invalid;
	}
	/*we have requested  valid gpios*/
	if (gpio_request(gpio_led, "led_gpio")) {
		pr_alert("Unable to request gpio %d", gpio_led);
		retval = -EINVAL;
		goto invalid;
	}
	if (gpio_request(gpio_in, "gpio_in")) {
		pr_alert("Unable to request gpio %d", gpio_in);
		retval = -EINVAL;
		goto cleanup;
	}

	/*set gpio direction*/
	dir_err = gpio_direction_output(gpio_led, value);
	if (dir_err < 0) {
		pr_alert("Impossible to set output direction");
		retval = -EINVAL;
		goto cleanup2;
	}
	gpio_export(gpio_in, false);
	dir_err = gpio_direction_input(gpio_in);
	if (dir_err < 0) {
		pr_alert("Impossible to set input direction");
		retval = -EINVAL;
		goto cleanup2;
	}
	gpio_set_debounce(gpio_in, 200);
	gpio_export(gpio_in, false);

	/*set up work queue*/
	wq = create_singlethread_workqueue("blink_wq");
	if (!wq) {
		pr_alert("unable to setup workqueue\n");
		retval = -EINVAL;
		goto cleanup2;
	}

	work = (struct work_struct *)
		kmalloc(sizeof(struct work_struct), GFP_KERNEL);
	if (!work)
		goto cleanup2;
	INIT_WORK(work, change_led_state);

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
	goto invalid;

destroy_workqueue:
	flush_workqueue(wq);
	destroy_workqueue(wq);
	kfree(work);
cleanup2:
	gpio_free(gpio_in);
cleanup:
	gpio_free(gpio_led);
invalid:
	return retval;
}

static void __exit gpio_exit(void)
{

	free_irq(irq_line, NULL);

	flush_workqueue(wq);
	destroy_workqueue(wq);
	kfree(work);
	gpio_free(gpio_in);
	gpio_free(gpio_led);
	return;

}

module_init(gpio_init);
module_exit(gpio_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO Interrupt example (LED on/off on a button press");
MODULE_AUTHOR("Samuele Baisi");
