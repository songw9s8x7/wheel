/*Copyright (c) 2014, NTD. All rights reserved.
*
* Change log
*
* Date	Author	Description
* ----------------------------------------------------------------------------
*07/03/2014	Frank wang	Add whell driver.
*/
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/firmware.h>
#include <linux/printk.h>

//The driver information of wheel
struct platform_wheel_data {
	int irq;
	const char *name;
	unsigned int gpio;
	unsigned int key_val;	
};
struct platform_wheel_data wheel_desc[2];
static struct work_struct wheel_work;
static struct timer_list wheel_timer;
static struct input_dev *g_input;

static void report_key(struct work_struct *work)
{
	unsigned int rising_edge = 0, level = 0;

	rising_edge = gpio_get_value(wheel_desc[0].gpio);
	level = gpio_get_value(wheel_desc[1].gpio);
	if (rising_edge){
		input_event(g_input,EV_KEY,wheel_desc[!level].key_val,1);
		input_event(g_input,EV_KEY,wheel_desc[!level].key_val,0);	
	}
	else {
/*
 *if It's falling edge than the we according the B channel's level to 
 *judge the wheel's direction 
 */
		input_event(g_input,EV_KEY,wheel_desc[level].key_val,1);
		input_event(g_input,EV_KEY,wheel_desc[level].key_val,0);
	}
	input_sync(g_input);	

}
static irqreturn_t wheel_irq(int irq,void *dev_id)
{

	//set 10ms delay time for dispel shake
	mod_timer(&wheel_timer,jiffies + HZ/100); 
	return IRQ_RETVAL(IRQ_HANDLED);
}

static  void wheel_timer_function(unsigned long data)
{
	schedule_work(&wheel_work);
}
	
/*
*Translate OpenFirmware node properties into platform_data
*/
#ifdef CONFIG_OF
static int wheel_get_devtree_pdata(struct device *dev)
{
	const struct device_node *node;
	struct device_node *pp = NULL;
	int i = 0;
	u32 key_val;

	node = dev->of_node;
	if (node == NULL)
		return -ENODEV;
	while ((pp = of_get_next_child(node, pp))){
		enum of_gpio_flags flags;
		if(!of_find_property(pp, "gpios", NULL)){
			dev_warn(dev,"Found button without wheel_gpio\n");
			continue;
		}
		wheel_desc[i].gpio = of_get_gpio_flags(pp, 0, &flags);
		if (of_property_read_u32(pp, "linux,code", &key_val)){
			dev_err(dev, "Button without keycode");
			return -ENODEV;
		}
		wheel_desc[i].key_val = key_val;
		wheel_desc[i].name = of_get_property(pp, "label", NULL);
		i++;
	}
	return 0;
}
#else

static int wheel_get_devtree_pdata(struct device *dev)
{
	return -ENODEV;
}


#endif
static int __devinit wheel_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	struct device *dev = &pdev->dev;
	int ret,i = 0;

	ret = wheel_get_devtree_pdata(dev);
	if(ret)
	{
		printk(KERN_INFO"devtree get failed.\n");
		return ret;
	}
	input = input_allocate_device();
	g_input = input;
	platform_set_drvdata(pdev, input);
	input->evbit[0] =
		BIT_MASK(EV_KEY) | BIT_MASK(EV_KEY); 
	input->name = "wheel-keys";
	input->phys = "wheel-keys/input0";
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;
	
	INIT_WORK(&wheel_work,report_key);
	init_timer(&wheel_timer);
	wheel_timer.function = wheel_timer_function;
	add_timer(&wheel_timer);

	ret = input_register_device(input);
	if (ret) {
		dev_err(dev, "Unable to register input device\n");
		goto input_failed;
	}
	for(i = 0; i < ARRAY_SIZE(wheel_desc); i++)
	{
		set_bit(wheel_desc[i].key_val,input->keybit);
		ret = gpio_request(wheel_desc[i].gpio,wheel_desc[i].name);
		if (ret < 0) {
			dev_err(dev,"gpio get failed.\n");
			goto gpio_failed;
		}
		ret = gpio_direction_input(wheel_desc[i].gpio);
		if (ret < 0) {
			dev_err(dev,"gpio direction get failed.\n");
			goto gpio_failed;
		}
	}
	wheel_desc[0].irq = gpio_to_irq(wheel_desc[0].gpio);
	ret = request_irq(wheel_desc[0].irq,wheel_irq,
		IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,wheel_desc[0].name,NULL);
	return 0;

gpio_failed:
	 for(i = 0; i < ARRAY_SIZE(wheel_desc); i++) {       
                 gpio_free(wheel_desc[i].gpio);
         }
	return ret;
input_failed:
	input_unregister_device(input);
	return ret;
}
static int __devinit wheel_remove(struct platform_device *pdev)
{
	struct input_dev *input_dev = platform_get_drvdata(pdev);
	int i;

	free_irq(wheel_desc[0].irq,NULL);
	for(i = 0; i < ARRAY_SIZE(wheel_desc); i++) {
		gpio_free(wheel_desc[i].gpio);
	}

	input_unregister_device(input_dev);
	del_timer(&wheel_timer);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id wheel_of_match[] = {
	{.compatible = "wheel-keys", },
	{ },
};
MODULE_DEVICE_TABLE(of, wheel_of_match);
#else
#define wheel_of_match NULL
#endif

static struct platform_driver wheel_driver = {
	.probe = wheel_probe,
	.remove = __devexit_p(wheel_remove),

	.driver = {
		.name     = "wheel_keys",
		.owner    = THIS_MODULE,
		.of_match_table = wheel_of_match,
	},
};


static int __init wheel_init(void)
{
	return platform_driver_register(&wheel_driver);
}

static void __exit wheel_exit(void)
{
	platform_driver_unregister(&wheel_driver);
}

module_init(wheel_init);
module_exit(wheel_exit);

MODULE_LICENSE("GPL");


