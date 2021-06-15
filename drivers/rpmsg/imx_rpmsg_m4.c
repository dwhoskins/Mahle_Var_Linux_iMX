// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/rpmsg.h>

#define MSG		"Connect"


static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);


char kernelbuff[1024];

static struct rpmsg_device * prpdev_m4;

struct file_operations fops = 
{
.owner	= THIS_MODULE,
.read = device_read,
.write = device_write,
.open = device_open,
.release = device_release
};



//--------------------------------------------------------------------------------------------------------
int device_open(struct inode *inode, struct file *fp)
{
	struct rpmsg_device * rpdev = prpdev_m4;

	printk("device_open called");
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static int device_release(struct inode *inode, struct file *fp)
{
	struct rpmsg_device * rpdev = prpdev_m4;
	
	printk("device_release called");
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static ssize_t device_read(struct file *fp, char *ch, size_t sz, loff_t *lofft)
{
	struct rpmsg_device * rpdev = prpdev_m4;
	
	printk("device_read called");      
	copy_to_user(ch, kernelbuff, 1024);
	return sz;
}

//--------------------------------------------------------------------------------------------------------
static ssize_t device_write(struct file *fp, const char *ch, size_t sz, loff_t * offset)
{
	struct rpmsg_device * rpdev = prpdev_m4;
	printk("device_write called");
		
	copy_from_user(kernelbuff, ch, sz);
	*offset+=sz;	
	
	err = rpmsg_send(rpdev->ept, kernelbuff, sz);		
	if(err)
	{
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	return sz;
}



//--------------------------------------------------------------------------------------------------------
static int rpmsg_m4_cb(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 src)
{
	
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static int rpmsg_m4_probe(struct rpmsg_device *rpdev)
{
	int err;	

	prpdev_m4 = NULL;


	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	/*
	 * send a message to our remote processor, and tell remote
	 * processor about this channel
	 */
	
	err = rpmsg_send(rpdev->ept, MSG, strlen(MSG));
	if(err)
	{
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	prpdev_m4 = rpdev;

	return 0;
}





//--------------------------------------------------------------------------------------------------------
static void rpmsg_m4_remove(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "rpmsg m4 driver is removed\n");
	prpdev_m4 = NULL;
}

//--------------------------------------------------------------------------------------------------------
static struct rpmsg_device_id rpmsg_driver_m4_id_table[] = 
{
	{ .name	= "M4_VCI" },
	{ },
};

//--------------------------------------------------------------------------------------------------------
static struct rpmsg_driver rpmsg_m4_driver = 
{
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_m4_id_table,
	.probe		= rpmsg_m4_probe,
	.callback	= rpmsg_m4_cb,
	.remove	= rpmsg_m4_remove,
};

//--------------------------------------------------------------------------------------------------------
static int __init rpmsg_m4_init(void)
{
	register_chrdev(500, "M4_VCI", &fops);
	return register_rpmsg_driver(&rpmsg_m4_driver);
}

//--------------------------------------------------------------------------------------------------------
static void __exit rpmsg_m4_exit(void)
{
	unregister_chrdev(500, "M4_VCI");
	unregister_rpmsg_driver(&rpmsg_m4_driver);		
}


module_init(rpmsg_m4_init);
module_exit(rpmsg_m4_exit);

MODULE_AUTHOR("Mahle Service Solutions");
MODULE_DESCRIPTION("iMX virtio remote processor messaging driver");
MODULE_LICENSE("GPL v2");
